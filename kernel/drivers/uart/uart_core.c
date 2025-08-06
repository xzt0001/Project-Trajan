#include "../../../include/types.h"
#include "../../../include/uart.h"
#include "../../../include/vmm.h"

// Global MMU state flag - Now imported from vmm.c
// static int mmu_enabled = 0; - Removed as it's now defined in vmm.c

// PL011 UART base address (QEMU's default)
#define UART0_BASE UART_PHYS

// The global variable g_uart_base is now defined in uart_globals.c

// Define register offsets
#define UART_DR_OFFSET     0x00    // Data Register
#define UART_FR_OFFSET     0x18    // Flag Register

// UART Flag Register bit masks
#define UART_FR_TXFF       (1 << 5)  // Transmit FIFO full
#define UART_FR_RXFE       (1 << 4)  // Receive FIFO empty

// Debug flag
#define DEBUG_UART_PUTS    DEBUG_UART_MODE

// Direct register access functions - more reliable than macros
static inline void uart_write_reg(uint32_t offset, uint32_t value) {
    *((volatile uint32_t*)(g_uart_base + (uintptr_t)offset)) = value;
}

static inline uint32_t uart_read_reg(uint32_t offset) {
    return *((volatile uint32_t*)(g_uart_base + (uintptr_t)offset));
}

// is_mmu_enabled() removed as we now access the global flag directly

void uart_init(void) {
    // QEMU's UART is already initialized
    // This function is now just a marker that we've reached this point
    // Don't print anything here
}

void uart_putc(char c) {
    // Wait until UART is ready to transmit
    while (uart_read_reg(UART_FR_OFFSET) & UART_FR_TXFF);
    // Write character to data register
    uart_write_reg(UART_DR_OFFSET, c);
}

void uart_puts(const char *str) {
    // Debug marker to identify string print start
    if (DEBUG_UART_PUTS) {
        uart_putc('[');  // Start marker
    }
    
    if (!str) return;  // Safety check for null pointer
    
    // Use different implementations based on MMU state with proper synchronization
    if (mmu_enabled) {
        // MMU is enabled, use late implementation for virtual addresses and our global buffers
        
        // Get access to the global buffers
        extern volatile char global_string_buffer[];
        
        // Copy the string to our global buffer for safety
        int i;
        for (i = 0; str[i] && i < 250; i++) {
            global_string_buffer[i] = str[i];
        }
        global_string_buffer[i] = '\0';
        
        // Clean the cache lines for the copied buffer
        for (uintptr_t addr = (uintptr_t)global_string_buffer; 
             addr < (uintptr_t)global_string_buffer + ((i / 64) + 1) * 64; 
             addr += 64) {
            // Clean the cache line to ensure it's in memory
            asm volatile("dc cvac, %0" :: "r" (addr));
        }
        asm volatile("dsb ish" ::: "memory");
        
        // Process each character from the global buffer
        for (i = 0; global_string_buffer[i]; i++) {
            if (global_string_buffer[i] == '\n') uart_putc('\r');
            uart_putc(global_string_buffer[i]);
        }
    } else {
        // Pre-MMU operation using direct access
        // Process each character
        while (*str) {
            if (*str == '\n') uart_putc('\r');  // Add carriage return
            uart_putc(*str++);
        }
    }
    
    // Debug marker to identify string print end
    if (DEBUG_UART_PUTS) {
        uart_putc(']');  // End marker
    }
}

// Safe UART puts wrapper that handles MMU transition
void safe_uart_puts(const char* str) {
    if (!str) return;
    
    if (mmu_enabled) {
        // MMU is on, use regular uart_puts
        uart_puts(str);
    } else {
        // MMU is off, use early version that works with physical addresses
        uart_puts_early(str);
    }
}

void uart_puthex(uint64_t value) {
    // Print "0x" prefix using putc directly
    uart_putc('0');
    uart_putc('x');
    
    // Print exactly 8 hex digits with leading zeros for addresses
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        
        // Convert digit to character and print it
        if (digit < 10) {
            uart_putc('0' + digit);
        } else {
            uart_putc('a' + (digit - 10));
        }
    }
}

// Function to print 64-bit value in hexadecimal format (16 digits)
void uart_print_hex(uint64_t value) {
    // Print all 16 hex digits with leading zeros for 64-bit values
    const int num_digits = 16;
    
    for (int i = (num_digits - 1) * 4; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        
        // Convert digit to character and print it
        if (digit < 10) {
            uart_putc('0' + digit);
        } else {
            uart_putc('A' + (digit - 10));  // Use capital letters
        }
    }
}

// Function to print a 64-bit value in hex format with proper formatting
void uart_hex64(uint64_t value) {
    char buf[17];
    buf[16] = '\0';
    
    for (int i = 15; i >= 0; i--) {
        int nibble = value & 0xF;
        buf[i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        value >>= 4;
    }
    
    uart_putc('0');
    uart_putc('x');
    
    // Loop through the buffer and output character by character
    for (int i = 0; i < 16; i++) {
        uart_putc(buf[i]);
    }
}

// Function to print a value in hex format (used in context.S and vmm.c)
void uart_putx(uint64_t value) {
    // Print 8 hex digits with no prefix
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        
        // Convert digit to character and print it
        if (digit < 10) {
            uart_putc('0' + digit);
        } else {
            uart_putc('A' + (digit - 10));  // Use capital letters
        }
    }
}

// Direct raw UART output function with no checks or waiting
void uart_putc_raw(char c) {
    volatile unsigned int* UART0_DR = (volatile unsigned int*)g_uart_base;
    *UART0_DR = (unsigned int)c;
}

// Initialize the PL011 UART with the specified base address
void init_uart_pl011(unsigned long uart_addr) {
    // Update the global UART base address if provided
    if (uart_addr != 0) {
        g_uart_base = (volatile uint32_t*)uart_addr;
    }
    
    // Simple test to confirm UART is working
    uart_putc_raw('U');
    uart_putc_raw('A');
    uart_putc_raw('R');
    uart_putc_raw('T');
    uart_putc_raw(':');
    uart_putc_raw('O');
    uart_putc_raw('K');
    uart_putc_raw('\r');
    uart_putc_raw('\n');
}

// Direct hardware access with no buffering or checks
void uart_putc_direct(char c) {
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS; // PL011 UART
    *UART0_DR = c;
    
    // Small inline delay to ensure character is transmitted
    for (volatile int i = 0; i < 1000; i++) { }
}

// Simple delay function to ensure UART transmission completes
void uart_delay(void) {
    for (volatile int i = 0; i < 10000; i++) { }
}

// Output a debug marker with clear visual separation
void uart_debug_marker(char marker) {
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS; // PL011 UART
    
    // Output a more distinct separator
    for (int i = 0; i < 3; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    // Output a very distinctive pattern
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = ' ';
    *UART0_DR = 'D';
    *UART0_DR = 'E';
    *UART0_DR = 'B';
    *UART0_DR = 'U';
    *UART0_DR = 'G';
    *UART0_DR = ' ';
    *UART0_DR = 'M';
    *UART0_DR = 'A';
    *UART0_DR = 'R';
    *UART0_DR = 'K';
    *UART0_DR = 'E';
    *UART0_DR = 'R';
    *UART0_DR = ':';
    *UART0_DR = ' ';
    *UART0_DR = marker;
    *UART0_DR = ' ';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    
    // More line breaks for separation
    for (int i = 0; i < 3; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    // Much longer delay to ensure character is transmitted
    for (volatile int i = 0; i < 50000; i++) {
        // Extended delay loop
    }
}

// Clear the terminal with many newlines to get a clean slate
void uart_clear_screen(void) {
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS;
    
    // Send many newlines to clear past output
    for (int i = 0; i < 50; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    // Send a clear header
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = ' ';
    *UART0_DR = 'C';
    *UART0_DR = 'L';
    *UART0_DR = 'E';
    *UART0_DR = 'A';
    *UART0_DR = 'R';
    *UART0_DR = ' ';
    *UART0_DR = 'O';
    *UART0_DR = 'U';
    *UART0_DR = 'T';
    *UART0_DR = 'P';
    *UART0_DR = 'U';
    *UART0_DR = 'T';
    *UART0_DR = ' ';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    
    // Give time for the UART to complete sending
    for (volatile int i = 0; i < 25000; i++) {
        // Delay
    }
}

// Direct pre-MMU versions of UART functions that operate directly on physical address
void uart_putc_early(char c) {
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS;
    volatile unsigned int *UART0_FR = (volatile unsigned int *)(UART_PHYS + UART_FR_OFFSET);
    
    // Wait until UART is ready to transmit
    while (*UART0_FR & UART_FR_TXFF);
    
    // Write character to data register
    *UART0_DR = c;
}

void uart_puts_early(const char *str) {
    if (!str) return;  // Safety check
    
    while (*str) {
        if (*str == '\n') uart_putc_early('\r');  // Add carriage return
        uart_putc_early(*str++);
    }
}

void uart_hex64_early(uint64_t value) {
    // Output "0x" prefix
    uart_putc_early('0');
    uart_putc_early('x');
    
    // Output all 16 hex digits
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        uart_putc_early(c);
    }
}

// Update UART base address (called after MMU is enabled)
void uart_set_base(void* addr) {
    // Debug output before changing the address
    uart_puts_early("[UART] Switching base address from 0x");
    uart_hex64_early((uint64_t)g_uart_base);
    uart_puts_early(" to 0x");
    uart_hex64_early((uint64_t)addr);
    uart_puts_early("\n");
    
    // FIXED ORDER: First set base address, then set flag
    
    // Add memory barriers to ensure all previous memory accesses are complete
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    // 1. Update the base address first
    g_uart_base = (volatile uint32_t*)addr;
    
    // More memory barriers to ensure the base address update is visible
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    // 2. Only then set MMU enabled flag
    mmu_enabled = true;
    
    // Final barrier to ensure flag update is visible
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    
    // Test output through the new address - use direct methods first
    volatile unsigned int* uart_dr = (volatile unsigned int*)UART_VIRT;
    
    // Output a test pattern directly to verify UART access works before calling uart_puts
    *uart_dr = 'T'; // T for Test
    *uart_dr = 'E';
    *uart_dr = 'S';
    *uart_dr = 'T';
    *uart_dr = ':';
    *uart_dr = 'O';
    *uart_dr = 'K';
    *uart_dr = '\r';
    *uart_dr = '\n';
    
    // Now try the regular uart_puts
    uart_puts("[UART] Base address updated successfully\n");
}

// UART panic function - print error and halt
void uart_panic(const char* str) {
    // Output directly to UART for maximum reliability
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS;
    
    // Output panic header
    const char* panic_header = "\r\n!!! PANIC: ";
    const char* c = panic_header;
    while (*c) *UART0_DR = *c++;
    
    // Output the panic message
    if (str) {
        c = str;
        while (*c) *UART0_DR = *c++;
    } else {
        const char* unknown = "Unknown error";
        c = unknown;
        while (*c) *UART0_DR = *c++;
    }
    
    // Output newline
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    
    // Halt the system
    while (1) {
        // Infinite loop
    }
}

// Initial UART setup - only used early in boot
void uart_init_early(unsigned long uart_addr) {
    // Simple test to confirm UART is working
    uart_putc_early('E'); // E for Early
    uart_putc_early('A'); // A for Access
    uart_putc_early('R'); // R for Ready
    uart_putc_early('L'); // L for Load
    uart_putc_early('Y'); // Y for Yes
    uart_putc_early(':'); // : separator
    uart_putc_early('O'); // O for OK
    uart_putc_early('K'); // K for K
    uart_putc_early('\r');
    uart_putc_early('\n');
}
