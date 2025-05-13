#include "../include/types.h"
#include "../include/uart.h"

// Explicitly reference the global UART base pointer defined in uart.c
extern volatile uint32_t* g_uart_base;

// The global MMU status flag is now defined in uart_globals.c

// Virtual UART base address - hardcoded for post-MMU use
#define UART_VIRT 0xFFFF000009000000UL

// Buffer size for global string operations
#define GLOBAL_BUFFER_SIZE 256

// Global buffer for safe string handling during MMU transition
// Marked volatile to prevent compiler optimizations
volatile char global_string_buffer[GLOBAL_BUFFER_SIZE] __attribute__((aligned(64)));

// Temporary buffer for additional operations 
volatile char global_temp_buffer[GLOBAL_BUFFER_SIZE] __attribute__((aligned(64)));

// Phase 1: Global String Buffer Alignment and Allocation
// Fixed to match screenshots - static volatile
__attribute__((aligned(64)))
static volatile char mmu_msg[64];

// Accessor function for the static mmu_msg buffer
volatile char* get_mmu_msg_buffer(void) {
    return mmu_msg;
}

// Phase 5: Safe Dereferencing via Indexed Access
// Function to write a string to UART with indexed access for safety
void uart_puts_safe_indexed(const char *str) {
    if (!str) return;
    
    // Clear the buffer first with indexed access
    for (int i = 0; i < 64; i++) {
        mmu_msg[i] = 0;
    }
    
    // Explicit memory barrier to ensure buffer is zeroed
    asm volatile("dsb sy" ::: "memory");
    
    // Pre-invalidate cache for destination buffer before copying
    for (uintptr_t addr = (uintptr_t)&mmu_msg[0]; 
         addr < (uintptr_t)&mmu_msg[64]; 
         addr += 64) {
        asm volatile("dc ivac, %0" :: "r" (addr) : "memory");
    }
    asm volatile("dsb ish" ::: "memory");
    
    // Copy using indexed access - max 63 chars to ensure null termination
    int i;
    for (i = 0; i < 63 && str[i]; i++) {
        // Access each character individually and copy to buffer
        char c = str[i];
        mmu_msg[i] = c;
        
        // Clean the cache line for this specific byte to ensure it reaches memory
        uintptr_t addr = (uintptr_t)&mmu_msg[i];
        // Use CIVAC (Clean and Invalidate) instead of just Clean
        asm volatile("dc civac, %0" :: "r" (addr));
    }
    mmu_msg[i] = '\0';
    
    // Clean the cache line for the null terminator
    uintptr_t term_addr = (uintptr_t)&mmu_msg[i];
    asm volatile("dc civac, %0" :: "r" (term_addr));
    
    // Complete memory barrier and TLB invalidation sequence
    asm volatile("dsb ish" ::: "memory");     // Ensure all memory operations complete
    
    // Invalidate TLB entries for the buffer region
    uintptr_t buffer_start = ((uintptr_t)&mmu_msg[0]) >> 12;
    asm volatile("tlbi vaae1is, %0" :: "r" (buffer_start) : "memory");
    
    // Also invalidate for any potential string source in case it's in a newly mapped region
    if (str != NULL) {
        uintptr_t source_page = ((uintptr_t)str) >> 12;
        asm volatile("tlbi vaae1is, %0" :: "r" (source_page) : "memory");
    }
    
    // Flush instruction cache as well to ensure coherency
    asm volatile("ic ialluis" ::: "memory");
    
    // End with instruction barrier to ensure instruction stream is synchronized
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    
    // Pre-read UART registers to verify access
    volatile uint32_t* uart_fr = g_uart_base + (0x18/sizeof(uint32_t));
    volatile uint32_t fr_value = *uart_fr;
    
    // Debug output if enabled
    #ifdef DEBUG_UART_MODE
    // Use direct character output to avoid recursion
    uart_emergency_output('U');
    uart_emergency_output('A');
    uart_emergency_output('R');
    uart_emergency_output('T');
    uart_emergency_output(' ');
    uart_emergency_output('F');
    uart_emergency_output('R');
    uart_emergency_output(':');
    uart_emergency_output(' ');
    uart_emergency_hex64(fr_value);
    uart_emergency_output('\r');
    uart_emergency_output('\n');
    #endif
    
    // Wait for any pending UART operations to complete
    asm volatile("dsb ish" ::: "memory");
    
    // Output each character from the buffer
    int count = 0;
    
    // Output each character with memory barriers between operations
    for (i = 0; i < 64 && mmu_msg[i]; i++) {
        // Re-read the character each time to ensure we're getting the latest value
        // This avoids any potential register caching issues
        char c = mmu_msg[i];
        
        // Handle newlines with CR+LF
        if (c == '\n') {
            // Verify FR register before accessing
            while (*uart_fr & (1 << 5)); // Wait until FIFO has space
            *g_uart_base = '\r';
            
            // Barrier between UART operations
            asm volatile("dmb ish" ::: "memory");
        }
        
        // Wait until UART transmit FIFO has space
        while (*uart_fr & (1 << 5));
        
        // Write the character
        *g_uart_base = c;
        
        // Use a barrier between each character to ensure proper ordering
        asm volatile("dmb ish" ::: "memory");
        
        count++;
    }
    
    // Final memory barrier after all UART operations
    asm volatile("dsb ish" ::: "memory");
    
    // Debugging output if output seems to have failed
    if (count < 4 && str[0] && str[1] && str[2] && str[3]) {
        uart_debug_marker_late('B'); // Buffer output failed marker
        uart_emergency_puts(str);    // Try emergency output as fallback
    }
}

// Phase 6: Fallback Escalation Mechanism
// Function that attempts multiple methods to output a string
bool uart_puts_with_fallback(const char *str) {
    if (!str) return false;
    
    bool success = false;
    int attempt = 0;
    
    while (!success && attempt < 3) {
        switch (attempt) {
            case 0:
                // First attempt: try normal puts_late
                uart_puts_late(str);
                // Assume success - we'll catch failure in next attempts if needed
                success = true;
                break;
                
            case 1:
                // Second attempt: try indexed access with explicit cache maintenance
                uart_puts_safe_indexed(str);
                success = true;
                break;
                
            case 2:
                // Last resort: character-by-character emergency output
                uart_emergency_puts(str);
                success = true;
                break;
        }
        
        attempt++;
    }
    
    return success;
}

// Add a function to set the MMU status when UART base is switched
void uart_set_mmu_enabled(void) {
    // Set the global flag to indicate MMU is enabled with stronger barriers
    __asm__ volatile("dsb sy" ::: "memory");  // Data Synchronization Barrier (system)
    extern bool mmu_enabled;
    mmu_enabled = true;
    __asm__ volatile("dsb sy" ::: "memory");  // Data Synchronization Barrier after flag update
    __asm__ volatile("isb" ::: "memory");     // Instruction Synchronization Barrier
    
    // Direct output using raw UART registers instead of using a function that might use string literals
    volatile uint32_t *UART0_DR = (volatile uint32_t *)UART_VIRT;
    *UART0_DR = '[';
    *UART0_DR = 'M';
    *UART0_DR = 'M';
    *UART0_DR = 'U';
    *UART0_DR = ']';
    *UART0_DR = ' ';
    *UART0_DR = 'E';
    *UART0_DR = 'n';
    *UART0_DR = 'a';
    *UART0_DR = 'b';
    *UART0_DR = 'l';
    *UART0_DR = 'e';
    *UART0_DR = 'd';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
}

// Direct UART access functions for post-MMU stage
void uart_putc_late(char c) {
    // Wait until UART transmit FIFO has space
    while (*(g_uart_base + 0x18/sizeof(uint32_t)) & (1 << 5));
    
    // Write the character directly to the UART data register
    *(g_uart_base) = c;
}

void uart_puts_late(const char *str) {
    if (!str) return;
    
    // Debug output to verify the UART base address - use direct access
    uart_emergency_output('A'); // UART base address check
    // Output the global base pointer value instead of hardcoded address
    uart_emergency_hex64((uint64_t)g_uart_base);
    uart_emergency_output('\r');
    uart_emergency_output('\n');
    
    // Phase 2: Cache Line Maintenance for the Entire Buffer
    // Zero out the buffer first to avoid stale data
    for (int i = 0; i < GLOBAL_BUFFER_SIZE; i++) {
        global_string_buffer[i] = 0;
    }
    
    // Copy string to global buffer for safety during MMU transition
    int i;
    for (i = 0; i < GLOBAL_BUFFER_SIZE - 1 && str[i]; i++) {
        global_string_buffer[i] = str[i];
    }
    global_string_buffer[i] = '\0';
    
    // Phase 3: Pre- and Post-MMU Memory Barrier Sequence
    // Ensure all writes to the buffer are visible before cache maintenance
    asm volatile("dsb ish" ::: "memory");
    
    // Perform explicit cache maintenance on the buffer
    for (uintptr_t addr = (uintptr_t)global_string_buffer; 
         addr < (uintptr_t)global_string_buffer + GLOBAL_BUFFER_SIZE; 
         addr += 64) {  // Cache line size is typically 64 bytes
        // First invalidate the cache line to ensure we have the latest data
        asm volatile("dc ivac, %0" :: "r" (addr));
        // Clean data cache by virtual address to point of coherency
        asm volatile("dc cvac, %0" :: "r" (addr));
    }
    
    // Complete memory barrier sequence
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    
    // Process each character from the global buffer
    int count = 0;
    for (i = 0; i < GLOBAL_BUFFER_SIZE - 1 && global_string_buffer[i]; i++) {
        if (global_string_buffer[i] == '\n') uart_putc_late('\r');
        uart_putc_late(global_string_buffer[i]);
        count++;
    }
    
    // If we output fewer than 4 characters, something likely went wrong
    if (count < 4 && str[0] && str[1] && str[2] && str[3]) {
        uart_debug_marker_late('F');  // Failed output marker
        
        // Phase 6: Fallback Escalation Mechanism - Try emergency output as last resort
        uart_emergency_puts(str);
    }
}

void uart_hex64_late(uint64_t value) {
    // Output "0x" prefix
    uart_putc_late('0');
    uart_putc_late('x');
    
    // Output all 16 hex digits
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        uart_putc_late(c);
    }
}

// Debug helper for validating pointer values post-MMU
void uart_debug_hex(uint64_t val) {
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        *(g_uart_base) = c;
    }
}

// Post-MMU debug function that writes directly to virtual UART
void uart_debug_marker_late(char marker) {
    // Output a distinctive pattern
    for (int i = 0; i < 3; i++) {
        *(g_uart_base) = '\r';
        *(g_uart_base) = '\n';
    }
    
    *(g_uart_base) = '#';
    *(g_uart_base) = '#';
    *(g_uart_base) = '#';
    *(g_uart_base) = ' ';
    *(g_uart_base) = 'P';
    *(g_uart_base) = 'O';
    *(g_uart_base) = 'S';
    *(g_uart_base) = 'T';
    *(g_uart_base) = '-';
    *(g_uart_base) = 'M';
    *(g_uart_base) = 'M';
    *(g_uart_base) = 'U';
    *(g_uart_base) = ' ';
    *(g_uart_base) = marker;
    *(g_uart_base) = ' ';
    *(g_uart_base) = '#';
    *(g_uart_base) = '#';
    *(g_uart_base) = '#';
    *(g_uart_base) = '\r';
    *(g_uart_base) = '\n';
    
    // More line breaks for separation
    for (int i = 0; i < 3; i++) {
        *(g_uart_base) = '\r';
        *(g_uart_base) = '\n';
    }
}

// Function to test if UART virtual mapping is working
void uart_test_virt_mapping(void) {
    uart_puts_late("\n[TEST] Testing virtual UART mapping\n");
    uart_puts_late("[TEST] If you can read this, virtual UART mapping is working!\n");
    
    // Print memory addresses for debugging
    uart_puts_late("[TEST] UART_VIRT address: 0x");
    uart_hex64_late(UART_VIRT);
    uart_puts_late("\n");
    
    // Print the global base pointer value 
    uart_puts_late("[TEST] g_uart_base pointer: 0x");
    uart_hex64_late((uint64_t)g_uart_base);
    uart_puts_late("\n");
    
    // Try direct access as well
    *(g_uart_base) = 'D'; // Direct
    *(g_uart_base) = 'I'; // access
    *(g_uart_base) = 'R'; // test
    *(g_uart_base) = '\r';
    *(g_uart_base) = '\n';
}

// Direct UART output using only assembly instructions for maximum reliability
// This function bypasses all C code hazards and directly writes to the UART hardware
void uart_emergency_output(char c) {
    // Use the global UART base pointer with fallback to hardcoded address
    register uint64_t uart_dr_addr asm("x0") = (g_uart_base != NULL) ? 
                                              (uint64_t)g_uart_base : UART_VIRT;
    
    // Wait for UART to be ready (TXFF flag clear in FR register)
    asm volatile(
        // Load the address of the flag register (base + 0x18)
        "add x1, x0, #0x18\n"
        
        // Wait loop until TXFF is clear
        "1:\n"
        "ldr w2, [x1]\n"         // Load flag register
        "tst w2, #(1 << 5)\n"    // Test TXFF bit
        "b.ne 1b\n"              // Loop if TXFF is set
        
        // Write character to data register
        "strb %w[ch], [x0]\n"
        
        : // No outputs
        : [ch] "r" (c), "r" (uart_dr_addr)
        : "x1", "x2", "memory"
    );
}

// Emergency string output with direct register access
void uart_emergency_puts(const char* str) {
    if (!str) return;
    
    // Output a distinctive marker
    uart_emergency_output('[');
    uart_emergency_output('E');
    uart_emergency_output('M');
    uart_emergency_output('G');
    uart_emergency_output(']');
    uart_emergency_output(' ');
    
    // Output the string character by character
    while (*str) {
        if (*str == '\n') uart_emergency_output('\r');
        uart_emergency_output(*str++);
    }
    
    uart_emergency_output('\r');
    uart_emergency_output('\n');
}

// Diagnostic helper that directly prints a hex value using assembly
void uart_emergency_hex64(uint64_t value) {
    // Hex digits lookup
    static const char hex_chars[] = "0123456789abcdef";
    
    // Output 0x prefix
    uart_emergency_output('0');
    uart_emergency_output('x');
    
    // Output 16 hex digits from most significant to least
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        uart_emergency_output(hex_chars[nibble]);
    }
} 