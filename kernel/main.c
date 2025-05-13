#include "../include/uart.h"
#include "../include/task.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/scheduler.h"
#include "../include/timer.h"
#include "../include/types.h"
#include "../include/interrupts.h"
#include "../include/string.h"  // Add string.h for memset

// Add the test_context_switch declaration
extern void test_context_switch(void);

// Add declaration for snprintf function
extern int snprintf(char* buffer, size_t count, const char* format, ...);

// Global volatile UART register - accessible throughout
volatile unsigned int* const GLOBAL_UART __attribute__((used)) = (volatile unsigned int*)0x09000000;

// Ensure kernel_main is properly exported - forward declaration with all attributes
__attribute__((used, externally_visible, noinline, section(".text.boot.main")))
void kernel_main(unsigned long uart_addr);

// Replace <stdbool.h> with our own boolean type definitions for freestanding environment
// REMOVED duplicate bool type definition - already in types.h

// Base address constants
#define UART0_BASE_ADDR 0x09000000
#define UART_DR_REG     (UART0_BASE_ADDR + 0x00)
#define UART_FR_REG     (UART0_BASE_ADDR + 0x18)
#define UART_FR_TXFF    (1 << 5)

// Vector table symbols defined in linker script
extern char vector_table[];            // Virtual address (0x1000000)
extern char _vector_table_load_start[]; // Physical load address (0x89000)

// Task functions declaration
void task_a(void) __attribute__((noreturn, used, externally_visible, section(".text")));

// Forward declarations
extern void full_restore_context(task_t* task);
extern uint64_t* get_kernel_page_table(void);  // Level 0 table
extern void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
extern void ensure_vector_table_executable_l3(uint64_t* l3_table);  // Use L3 table version
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);

// Function declarations for init functions
extern void init_pmm(void);
extern void init_vmm(void);
extern void init_vmm_wrapper(void); // Add wrapper function declaration
extern void create_task(void (*entry_point)()); // Corrected function declaration

// Flags for mapping memory (copied from vmm.c)
#define PTE_VALID       (1UL << 0)  // Entry is valid
#define PTE_TABLE       (1UL << 1)  // Entry points to another table (vs block/page)
#define PTE_AF          (1UL << 10) // Access flag - set when page is accessed
// REMOVED conflicting PTE_SH_INNER definition - use the one from vmm.h
#define PTE_AP_RW       (0UL << 6)  // Access permissions: Read/Write at any EL
#define ATTR_NORMAL_IDX 0           // Index for normal memory attributes
#define ATTR_NORMAL     (ATTR_NORMAL_IDX << 2)  // Normal memory attribute
#define PTE_PXN         (1UL << 53) // Privileged Execute Never (kernel can't execute)
#define PTE_UXN         (1UL << 54) // Unprivileged Execute Never (user can't execute)
#define PAGE_SIZE       4096        // Standard 4KB page size

// Forward declarations for functions used
void init_exceptions(void);
void init_scheduler(void);
void start_scheduler(void);
void panic(const char *message);
void set_vbar_el1(unsigned long addr);

// Define DEBUG_UART if not defined elsewhere
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// Direct UART access for diagnostics - doesn't rely on uart.c functions
void debug_print(const char* msg) {
    // Pointer to UART Data Register (where we write characters)
    volatile uint32_t* dr = (volatile uint32_t*)UART_DR_REG;
    // Pointer to UART Flag Register (status checks)
    volatile uint32_t* fr = (volatile uint32_t*)UART_FR_REG;
    
    // Safety check for null pointer
    if (!msg) return;
    
    // Loop through each character in the message
    while (*msg) {
        // Handle newline characters by adding carriage return
        if (*msg == '\n') {
            // Wait until TX FIFO has space (bit 5 = TXFF flag)
            while ((*fr) & UART_FR_TXFF);
            // Send carriage return before newline
            *dr = '\r';
        }
        
        // Wait for TX FIFO to have space again
        while ((*fr) & UART_FR_TXFF);
        // Send current character and move to next
        *dr = *msg++;
    }
}

// Utility function to print a 64-bit value in hex with a label
void debug_hex64(const char* label, uint64_t value) {
    // Print the label first
    debug_print(label);
    
    // Direct UART access for raw hex output
    volatile uint32_t* dr = (volatile uint32_t*)UART_DR_REG;
    volatile uint32_t* fr = (volatile uint32_t*)UART_FR_REG;
    
    // Output "0x" prefix
    while ((*fr) & UART_FR_TXFF);
    *dr = '0';
    while ((*fr) & UART_FR_TXFF);
    *dr = 'x';
    
    // Print all 16 hex digits (64 bits)
    for (int i = 15; i >= 0; i--) {
        // Extract each hex digit
        int digit = (value >> (i * 4)) & 0xF;
        char hex_char = digit < 10 ? ('0' + digit) : ('A' + (digit - 10));
        
        // Output to UART
        while ((*fr) & UART_FR_TXFF);
        *dr = hex_char;
    }
    
    // Add newline
    while ((*fr) & UART_FR_TXFF);
    *dr = '\r';
    while ((*fr) & UART_FR_TXFF);
    *dr = '\n';
}

// Decode and print a page table entry's flags
void decode_pte(uint64_t pte) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Output PTE value first
    debug_hex64("PTE value: ", pte);
    
    // Decode important flag bits
    *uart = 'F'; *uart = 'L'; *uart = 'A'; *uart = 'G'; *uart = 'S'; *uart = ':'; *uart = ' ';
    
    // Valid bit (bit 0)
    if (pte & (1UL << 0)) { *uart = 'V'; } else { *uart = 'v'; }
    
    // Table bit (bit 1) - is this a block or table?
    if (pte & (1UL << 1)) { *uart = 'T'; } else { *uart = 'B'; }
    
    // Access permissions (bits 6-7)
    uint8_t ap = (pte >> 6) & 0x3;
    if (ap == 0) { *uart = 'W'; } // RW at all levels
    else if (ap == 1) { *uart = 'R'; } // RO at all levels
    else if (ap == 2) { *uart = 'w'; } // RW EL1 only, no EL0 access
    else { *uart = 'r'; } // RO EL1 only, no EL0 access
    
    // Shareability (bits 8-9)
    uint8_t sh = (pte >> 8) & 0x3;
    if (sh == 0) { *uart = 'N'; }      // Non-shareable
    else if (sh == 1) { *uart = 'O'; } // Outer shareable
    else if (sh == 2) { *uart = 'I'; } // Inner shareable
    else { *uart = 'S'; }              // Reserved
    
    // Access flag (bit 10)
    if (pte & (1UL << 10)) { *uart = 'A'; } else { *uart = 'a'; }
    
    // PXN - Privileged Execute Never (bit 53)
    if (pte & (1UL << 53)) { *uart = 'P'; } else { *uart = 'p'; }
    
    // UXN - Unprivileged Execute Never (bit 54)
    if (pte & (1UL << 54)) { *uart = 'U'; } else { *uart = 'u'; }
    
    // Memory attributes index (bits 2-4)
    uint8_t attrindx = (pte >> 2) & 0x7;
    *uart = '0' + attrindx;
    
    *uart = '\r';
    *uart = '\n';
}

// Print detailed information about a page mapping
void dump_page_mapping(const char* label, uint64_t virt_addr) {
    debug_print("\n--------------------------------------------\n");
    debug_print(label);
    debug_print("\n--------------------------------------------\n");
    
    // Print the address being examined
    debug_hex64("Virtual address: ", virt_addr);
    
    // Calculate page table indices
    uint64_t page_addr = virt_addr & ~0xFFFUL; // Page-aligned address
    uint64_t l3_index = (page_addr >> 12) & 0x1FF;
    uint64_t l2_index = (page_addr >> 21) & 0x1FF;
    uint64_t l1_index = (page_addr >> 30) & 0x1FF;
    uint64_t l0_index = (page_addr >> 39) & 0x1FF;
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Print indices
    *uart = 'L'; *uart = '0'; *uart = ':'; 
    for (int i = 8; i >= 0; i--) {
        int bit = (l0_index >> i) & 1;
        *uart = '0' + bit;
    }
    *uart = ' ';
    
    *uart = 'L'; *uart = '1'; *uart = ':';
    for (int i = 8; i >= 0; i--) {
        int bit = (l1_index >> i) & 1;
        *uart = '0' + bit;
    }
    *uart = ' ';
    
    *uart = 'L'; *uart = '2'; *uart = ':';
    for (int i = 8; i >= 0; i--) {
        int bit = (l2_index >> i) & 1;
        *uart = '0' + bit;
    }
    *uart = ' ';
    
    *uart = 'L'; *uart = '3'; *uart = ':';
    for (int i = 8; i >= 0; i--) {
        int bit = (l3_index >> i) & 1;
        *uart = '0' + bit;
    }
    *uart = '\r';
    *uart = '\n';
    
    // Get kernel page table root and navigate the tables
    uint64_t* l0 = (uint64_t*)get_kernel_page_table();
    debug_hex64("L0 table: ", (uint64_t)l0);
    
    if (!l0) {
        debug_print("ERROR: L0 table is NULL!\n");
        return;
    }
    
    // Check L0 entry
    uint64_t l0_entry = l0[l0_index];
    debug_hex64("L0 entry: ", l0_entry);
    
    if (!(l0_entry & PTE_VALID)) {
        debug_print("ERROR: L0 entry not valid!\n");
        return;
    }
    
    // Access L1 table
    uint64_t* l1 = (uint64_t*)((l0_entry & ~0xFFFUL));
    debug_hex64("L1 table: ", (uint64_t)l1);
    
    // Check L1 entry
    uint64_t l1_entry = l1[l1_index];
    debug_hex64("L1 entry: ", l1_entry);
    
    if (!(l1_entry & PTE_VALID)) {
        debug_print("ERROR: L1 entry not valid!\n");
        return;
    }
    
    // Access L2 table
    uint64_t* l2 = (uint64_t*)((l1_entry & ~0xFFFUL));
    debug_hex64("L2 table: ", (uint64_t)l2);
    
    // Check L2 entry
    uint64_t l2_entry = l2[l2_index];
    debug_hex64("L2 entry: ", l2_entry);
    
    if (!(l2_entry & PTE_VALID)) {
        debug_print("ERROR: L2 entry not valid!\n");
        return;
    }
    
    // Access L3 table
    uint64_t* l3 = (uint64_t*)((l2_entry & ~0xFFFUL));
    debug_hex64("L3 table: ", (uint64_t)l3);
    
    // Check L3 entry
    uint64_t l3_entry = l3[l3_index];
    debug_hex64("L3 entry: ", l3_entry);
    
    if (!(l3_entry & PTE_VALID)) {
        debug_print("ERROR: L3 entry not valid!\n");
        return;
    }
    
    // Decode the final PTE
    debug_print("PTE flags:\n");
    decode_pte(l3_entry);
    
    // Physical address derived from entry
    uint64_t phys_addr = l3_entry & ~0xFFFUL;
    debug_hex64("Maps to physical: ", phys_addr);
    
    debug_print("--------------------------------------------\n");
}

// Task functions with direct UART output for maximum visibility
// Use attributes to ensure the function is properly visible to the linker
void task_a(void) __attribute__((noreturn, used, externally_visible, section(".text")));
void task_a(void) {
    // Very first output - immediate raw UART trace at entry point
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Entry markers to confirm we've reached task_a
    *uart = 'A'; 
    *uart = '_';
    *uart = 'S';
    *uart = 'T';
    *uart = 'A';
    *uart = 'R';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    // Minimal task loop - just print 'A' continuously
    while (1) {
        *uart = 'A';
        
        // Simple delay to slow down output
        for (volatile int i = 0; i < 100000; i++) {
            // Empty busy-wait
        }
    }
}

void task_b(void) __attribute__((noreturn, used, externally_visible, section(".text")));
void task_b(void) {
    // Very first output - immediate raw UART trace at entry point
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Entry markers to confirm we've reached task_b
    *uart = 'B'; 
    *uart = '_';
    *uart = 'S';
    *uart = 'T';
    *uart = 'A';
    *uart = 'R';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    // Minimal task loop - just print 'B' continuously
    while (1) {
        *uart = 'B';
        
        // Simple delay to slow down output
        for (volatile int i = 0; i < 100000; i++) {
            // Empty busy-wait
        }
    }
}

// Verify that VBAR_EL1 is set correctly
void verify_vbar_el1(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = '_'; *uart = 'C'; *uart = 'H'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    uint64_t vbar;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    
    uint64_t vector_addr = (uint64_t)vector_table;
    
    debug_print("[VBAR] Verifying VBAR_EL1\n");
    debug_print("[VBAR] Expected: ");
    debug_hex64("", vector_addr);
    debug_print("[VBAR] Actual: ");
    debug_hex64("", vbar);
    
    if (vbar == vector_addr) {
        debug_print("[VBAR] VBAR_EL1 correctly set ✓\n");
        
        // Check alignment
        if ((vbar & 0x7FF) == 0) {
            debug_print("[VBAR] Vector table is 2KB aligned ✓\n");
        } else {
            debug_print("[VBAR] ERROR: Vector table not 2KB aligned!\n");
        }
        
        // Also try to read first word of vector table to confirm memory access
        volatile uint32_t* vt_ptr = (volatile uint32_t*)vbar;
        uint32_t first_word = *vt_ptr;
        debug_print("[VBAR] First word of vector table: ");
        debug_hex64("", first_word);
    } else {
        debug_print("[VBAR] ERROR: VBAR_EL1 mismatch!\n");
    }
}

// Verify and explicitly fix vector table mapping
void verify_and_fix_vector_table(void) {
    // Use the globally declared vector_table
    uint64_t vector_addr = (uint64_t)vector_table;
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    // Print vector table address
    *uart = 'V'; *uart = 'T'; *uart = '='; *uart = ' ';
    
    // Print hex address (simplified for brevity)
    uint32_t addr_high = (vector_addr >> 32) & 0xFFFFFFFF;
    uint32_t addr_low = vector_addr & 0xFFFFFFFF;
    
    // Print high word (only if non-zero)
    if (addr_high) {
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nibble = (addr_high >> i) & 0xF;
            *uart = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
        }
    }
    
    // Print low word
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (addr_low >> i) & 0xF;
        *uart = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
    }
    
    *uart = '\r'; *uart = '\n';
    
    // Verify alignment
    if (vector_addr & 0x7FF) {
        // Not aligned to 2KB boundary
        *uart = 'V'; *uart = 'T'; *uart = '_'; *uart = 'A'; *uart = 'L'; *uart = 'N'; *uart = '!';
        *uart = '\r'; *uart = '\n';
    }
    
    // Get the first word of the vector table to see if it's accessible
    volatile uint32_t* vt_ptr = (volatile uint32_t*)vector_addr;
    uint32_t first_word = *vt_ptr;  // Try to read
    
    // Print first word
    *uart = 'V'; *uart = 'T'; *uart = '['; *uart = '0'; *uart = ']'; *uart = '='; *uart = ' ';
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (first_word >> i) & 0xF;
        *uart = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
    }
    *uart = '\r'; *uart = '\n';
    
    // Access the sync exception vector (should be at offset 0x200)
    volatile uint32_t* sync_vec_ptr = (volatile uint32_t*)(vector_addr + 0x200);
    uint32_t sync_word = *sync_vec_ptr;  // Try to read
    
    // Print sync vector first word
    *uart = 'S'; *uart = 'Y'; *uart = 'N'; *uart = 'C'; *uart = '='; *uart = ' ';
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (sync_word >> i) & 0xF;
        *uart = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
    }
    *uart = '\r'; *uart = '\n';
    
    // Now explicitly map vector_table with executable permissions using ensure_vector_table_executable_l3
    uint64_t* kernel_pt = get_kernel_page_table();
    if (!kernel_pt) {
        *uart = 'N'; *uart = 'O'; *uart = 'P'; *uart = 'T'; 
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Ensure vector table is executable
    uint64_t* l3_table = get_l3_table_for_addr(kernel_pt, vector_addr);
    if (l3_table) {
        ensure_vector_table_executable_l3(l3_table);
        debug_print("[VBAR] Vector table mapping secured\n");
    } else {
        debug_print("[VBAR] ERROR: Could not get L3 table for vector table!\n");
        while(1) {}
    }
}

// Direct UART write function that avoids inline assembly but ensures visibility
void __attribute__((noinline, used)) write_uart(char c) {
    *GLOBAL_UART = c;
}

// Simple string writer that can't be optimized out
void __attribute__((noinline, used)) write_string(const char* str) {
    while (*str) {
        write_uart(*str++);
    }
}

// Declare the assembly function
extern void test_uart_directly(void);

// ==========================================
// APPROACH 1: SIMPLIFIED EXCEPTION INITIALIZATION
// ==========================================
void init_exceptions_minimal(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'M'; *uart = 'E'; *uart = 'X'; *uart = 'C'; *uart = ':'; // Minimal exception setup
    
    // Get the vector table address
    uint64_t vector_addr = (uint64_t)vector_table;
    
    // Check vector table alignment (must be 2KB aligned = 0x800)
    if (vector_addr & 0x7FF) {
        *uart = 'A'; *uart = 'L'; *uart = 'N'; *uart = '!'; // Alignment error
        while(1) {} // Hang if alignment is wrong
    }
    
    // Just set the VBAR_EL1 register directly with minimal code
    asm volatile(
        "msr vbar_el1, %0\n"
        "isb\n"
        :: "r" (vector_addr)
    );
    
    // Verify VBAR_EL1 was set correctly
    uint64_t vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    
    if (vbar != vector_addr) {
        *uart = 'V'; *uart = 'B'; *uart = 'R'; *uart = '!'; // VBAR mismatch
        while(1) {} // Hang if verification fails
    }
    
    // Successfully initialized
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
}

void init_scheduler(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'I'; *uart = 'N'; *uart = 'I'; *uart = 'T'; *uart = '_'; *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = '\r'; *uart = '\n';
}

void start_scheduler(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T'; *uart = '_'; *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = '\r'; *uart = '\n';
    
    // Check VBAR_EL1 at scheduler start
    uint64_t vbar_check;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar_check));
    uart_puts("[SCHED] VBAR_EL1 at scheduler start: 0x");
    uart_hex64(vbar_check);
    uart_puts("\n");
    
    // Ensure correct VBAR_EL1 setting
    if (vbar_check != (uint64_t)vector_table) {
        uart_puts("[SCHED] ERROR: VBAR_EL1 incorrect at scheduler start! Fixing...\n");
        asm volatile("msr vbar_el1, %0" :: "r"(vector_table));
        asm volatile("isb");
    }
    
    // Create a task_t structure for task_a and jump to it
    extern void task_a(void);
    task_t test_task;
    
    // Clear the task structure
    for (int i = 0; i < sizeof(test_task)/sizeof(uint64_t); i++) {
        ((uint64_t*)&test_task)[i] = 0;
    }
    
    // Set up stack pointer (use a fixed stack area)
    uint64_t* test_stack = (uint64_t*)0x90000; // Use a known address
    test_task.stack_ptr = test_stack;
    
    // Set up PC
    test_task.pc = (uint64_t)task_a;
    
    // Set up SPSR for EL1h with interrupts enabled
    test_task.spsr = 0x345;
    
    // Test if we can directly call the dummy_task_a function
    uart_puts("[DEBUG] Testing direct call to dummy_task_a()...\n");
    extern void dummy_task_a(void);
    
    // Call the function directly - if it prints 'A', the function is sound
    // If it crashes, PC or memory permissions are invalid
    dummy_task_a();
    
    uart_puts("[DEBUG] Returned from direct function call\n");
    
    // Try calling dummy_asm first as a sanity test
    extern void dummy_asm(void);
    uart_puts("[DEBUG] Calling dummy_asm...\n");
    dummy_asm();  // If this prints 'Z', code execution path is valid
    uart_puts("[DEBUG] Returned from dummy_asm\n");
    
    // Try the known branch test function
    extern void known_branch_test(void);
    uart_puts("[DEBUG] Calling known_branch_test...\n");
    known_branch_test();  // If this prints 'X', code execution path is valid
    uart_puts("[DEBUG] Returned from known_branch_test\n");
    
    // Try our minimal context switch test first
    uart_puts("[DEBUG] Trying minimal context switch test...\n");
    extern void test_context_switch(void);
    extern void dummy_asm(void);

    // Debug checks for proper mapping and permissions
    uart_puts("[DEBUG] Checking dummy_asm address mapping\n");
    uint64_t dummy_addr = (uint64_t)dummy_asm;
    char buf[128];
    snprintf(buf, sizeof(buf), "[DEBUG] dummy_asm @ 0x%lx\n", dummy_addr);
    uart_puts(buf);

    // Verify dummy_asm is mapped with proper permissions
    extern void debug_check_mapping(uint64_t addr, const char* name);
    debug_check_mapping(dummy_addr, "dummy_asm");

    // Direct marker immediately before the call
    uart_putc('B');  // BEFORE test_context_switch

    test_context_switch();  // no logic, just jump
    
    // We shouldn't get here if the test worked
    uart_puts("[DEBUG] RETURNED FROM TEST CONTEXT SWITCH!\n");
    
    // This is a one-way jump that transfers control to the task
    full_restore_context(&test_task);
    
    // Should never reach here
    *uart = '!';  // Error
}

void panic(const char* message) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'P'; *uart = 'A'; *uart = 'N'; *uart = 'I'; *uart = 'C'; *uart = ':'; *uart = ' ';
    
    // Output message
    if (message) {
        while (*message) {
            *uart = *message++;
        }
    }
    
    *uart = '\r'; *uart = '\n';
    
    // Hang indefinitely
    while (1) {
        // Do nothing
    }
}

// Include these specific register definitions
#define VECTOR_ADDR_ALIGNMENT 0x800 // 2KB alignment
#define SCTLR_EL1_M (1 << 0)  // MMU enable bit
#define DAIF_IRQ_BIT (1 << 7) // IRQ bit in DAIF register

// External references to vector table
extern void set_vbar_el1(unsigned long addr);

// Reference to the task function
extern void task_a();

// Test function to generate exceptions for testing purposes
void test_exception_delivery(void) {
    debug_print("\n===== TESTING EXCEPTION DELIVERY =====\n");
    
    // Print current CPU state
    uint64_t currentEL, daif, vbar, sctlr;
    asm volatile("mrs %0, CurrentEL" : "=r"(currentEL));
    asm volatile("mrs %0, DAIF" : "=r"(daif));
    asm volatile("mrs %0, VBAR_EL1" : "=r"(vbar));
    asm volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
    
    debug_print("Current EL:   0x");
    uart_print_hex(currentEL);
    debug_print("\nDAIF:         0x");
    uart_print_hex(daif);
    debug_print("\nVBAR_EL1:     0x");
    uart_print_hex(vbar);
    debug_print("\nSCTLR_EL1:    0x");
    uart_print_hex(sctlr);
    debug_print("\n");
    
    // Record whether MMU is on
    bool mmu_enabled = (sctlr & SCTLR_EL1_M) != 0;
    debug_print("MMU is ");
    debug_print(mmu_enabled ? "ENABLED\n" : "DISABLED\n");
    
    // Test 1: Test SVC exception
    debug_print("\nTest 1: Generating SVC instruction...\n");
    asm volatile("svc #0");
    debug_print("Returned from SVC handler\n");
    
    // Test 2: Try to trigger timer interrupt manually
    debug_print("\nTest 2: Manually forcing timer interrupt...\n");
    force_timer_interrupt();
    debug_print("Returned from manual timer interrupt test\n");
    
    // Test 3: Verify if interrupts are enabled or not
    uint64_t daif_after;
    asm volatile("mrs %0, DAIF" : "=r"(daif_after));
    bool irqs_enabled = (daif_after & DAIF_IRQ_BIT) == 0;
    debug_print("\nTest 3: Checking IRQ state: IRQs are ");
    debug_print(irqs_enabled ? "ENABLED\n" : "DISABLED\n");
    
    if (!irqs_enabled) {
        debug_print("Enabling interrupts now...\n");
        enable_irq();
        
        // Verify again
        asm volatile("mrs %0, DAIF" : "=r"(daif_after));
        irqs_enabled = (daif_after & DAIF_IRQ_BIT) == 0;
        debug_print("IRQs now: ");
        debug_print(irqs_enabled ? "ENABLED\n" : "DISABLED\n");
    }
    
    // Test 4: Call IRQ handler directly to validate it works
    debug_print("\nTest 4: Directly calling IRQ handler...\n");
    test_irq_handler();
    debug_print("Returned from direct IRQ handler call\n");
    
    // Test 5: Verify VBAR_EL1 mapping by reading through the vector table
    debug_print("\nTest 5: Reading through vector table mapping...\n");
    uint64_t expected_address = (uint64_t)vector_table;
    debug_print("Expected vector table addr: 0x");
    uart_print_hex(expected_address);
    debug_print("\n");
    
    if (expected_address != vbar) {
        debug_print("WARNING: VBAR_EL1 doesn't match vector table address!\n");
    }
    
    // Try to read first few words of the vector table
    volatile uint32_t* vt_ptr = (volatile uint32_t*)vbar;
    debug_print("Reading vector table at 0x");
    uart_print_hex((uint64_t)vt_ptr);
    debug_print(":\n");
    
    // Try to safely read first 8 words
    for (int i = 0; i < 8; i++) {
        debug_print("  [");
        uart_print_hex(i * 4);
        debug_print("]: 0x");
        
        // This try-catch is conceptual since we don't have exceptions in C
        // Instead, just try to read and check if we're still executing
        volatile uint32_t word = vt_ptr[i];
        
        uart_print_hex(word);
        debug_print("\n");
    }
    
    debug_print("\n===== EXCEPTION TESTING COMPLETE =====\n\n");
}

// Simplest possible function that just outputs to UART
void test_uart_direct(void) {
    // Direct UART output using volatile pointer to ensure memory access
    volatile unsigned int* uart = (volatile unsigned int*)0x09000000;
    
    // Simple sequential characters for visibility
    *uart = 'T';  // T for Test
    *uart = 'U';  // U for UART
    *uart = 'D';  // D for Direct
    *uart = '!';  // ! for emphasis
    
    // Nothing else - just return
    return;
}

// Ultra-minimal scheduler test that just prints characters and returns
void test_scheduler_minimal(void) {
    // Direct UART access with minimal code
    volatile unsigned int* uart = (unsigned int*)0x09000000;
    
    // Print minimal markers
    *uart = 'M';  // M for Minimal test
    *uart = 'S';  // S for Scheduler
    *uart = 'T';  // T for Test
    
    // That's it - return immediately without doing anything complex
    return;
}

// Function to test the round-robin scheduler - minimal version
void test_scheduler(void) {
    volatile unsigned int* uart = (volatile uint32_t*)0x09000000;
    
    // Simple output directly to UART
    *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = 'D'; *uart = ':';
    
    // Just test returning from a function
    *uart = 'D';
    *uart = 'O';
    *uart = 'N';
    *uart = 'E';
    *uart = '!';
    *uart = '\r';
    *uart = '\n';
    
    // Return without doing anything complex
    return;
}

// Check and restore VBAR_EL1 if needed
void ensure_vbar_el1(void) {
    uint64_t current_vbar;
    uint64_t expected_vbar = (uint64_t)vector_table;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    uart_puts("[VBAR] Checking VBAR_EL1...\n");
    uart_puts("[VBAR] Current:  0x");
    uart_hex64(current_vbar);
    uart_puts("\n[VBAR] Expected: 0x");
    uart_hex64(expected_vbar);
    uart_puts("\n");
    
    if (current_vbar != expected_vbar) {
        uart_puts("[VBAR] CRITICAL: VBAR_EL1 was changed! Restoring...\n");
        // Reset it to the correct value
        asm volatile("msr vbar_el1, %0" :: "r"(expected_vbar));
        asm volatile("isb");
        
        // Verify it was properly set
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        uart_puts("[VBAR] After reset: 0x");
        uart_hex64(current_vbar);
        uart_puts("\n");
    } else {
        uart_puts("[VBAR] VBAR_EL1 is correctly set\n");
    }
}

// Initialize trap handlers
void init_traps(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'I'; *uart = 'T'; *uart = 'R'; *uart = 'P'; *uart = ':'; // Init trap marker
    
    // Get the saved vector table address from VMM (defined in vmm.c)
    extern uint64_t saved_vector_table_addr;
    
    // Use the fixed virtual address where we mapped the vector table
    uint64_t vt_addr = 0x1000000; // Default fixed address
    
    // If VMM saved a different address, use that (unlikely, but for robustness)
    if (saved_vector_table_addr != 0) {
        vt_addr = saved_vector_table_addr;
    }
    
    // Print the address we're about to use
    uart_puts("[VBAR] Setting vector table to: 0x");
    uart_hex64(vt_addr);
    uart_puts("\n");
    
    // Set VBAR_EL1 to the mapped virtual address
    asm volatile("msr vbar_el1, %0" :: "r"(vt_addr));
    asm volatile("isb");
    
    // Verify value was set
    uint64_t vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    
    // Add verification code as shown in the screenshot
    uart_puts("[VBAR] Set VBAR_EL1 to 0x1000000, read back 0x");
    uart_hex64(vbar);
    uart_puts("\n");
    
    // Verify VBAR_EL1 is set to 0x1000000
    if (vbar != 0x1000000) {
        uart_puts("[VBAR] ERROR: VBAR_EL1 not set to 0x1000000! Current value: 0x");
        uart_hex64(vbar);
        uart_puts("\n");
        
        // Try setting it again
        uart_puts("[VBAR] Attempting to set VBAR_EL1 one more time...\n");
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(vt_addr)
        );
        
        // Check again
        asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
        uart_puts("[VBAR] After second attempt: 0x");
        uart_hex64(vbar);
        uart_puts("\n");
    } else {
        uart_puts("[VBAR] SUCCESS: VBAR_EL1 correctly set to 0x1000000\n");
    }
}

// Add declaration for the EL0 task creation function and user_task
extern void create_el0_task(void (*entry_point)()); 
extern void user_test_svc(void);  // Changed from user_task to user_test_svc

// Vector table copy symbols defined in linker script
extern char _vector_table_source_start[];
extern char _vector_table_source_end[];
extern char _vector_table_dest_start[];

// Function to copy vector table from physical load address to target physical address if needed
void copy_vector_table_to_ram_if_needed(void) {
    extern char _vector_table_load_start[];  // From linker script
    char* src = _vector_table_load_start;    // Physical load address from linker
    char* dst = (char*)0x89000;              // Physical destination address
    size_t size = 0x800;                     // 2KB vector table size
    
    uart_puts("[BOOT] Vector table copy check: LOAD_ADDR=");
    uart_hex64((uint64_t)src);
    uart_puts(" DST=");
    uart_hex64((uint64_t)dst);
    uart_puts("\n");
    
    // Check if source and destination are the same - if so, no copy needed
    if (src == dst) {
        uart_puts("[BOOT] Vector table already at correct physical address, no copy needed\n");
        return;
    }
    
    // Simple validity check - only copy if destination appears to be zeroed or invalid
    volatile uint32_t* dst_word = (volatile uint32_t*)dst;
    if ((*dst_word & 0xFC000000) != 0x14000000) {
        uart_puts("[BOOT] Vector table at 0x89000 doesn't contain valid branch instruction, copying...\n");
        
        // Copy 2KB (vector table size)
        for (size_t i = 0; i < size; i++) {
            dst[i] = src[i];
        }
        
        uart_puts("[BOOT] Vector table copied to 0x89000\n");
        
        // Verify first instruction at destination (should be a branch)
        uint32_t first_instr = *(volatile uint32_t*)dst;
        uart_puts("[BOOT] First word at destination: ");
        uart_hex64(first_instr);
        uart_puts("\n");
        
        // Verify it looks like an ARM64 branch instruction
        if ((first_instr & 0xFC000000) == 0x14000000) {
            uart_puts("[BOOT] Copy successful - found valid ARM64 branch instruction\n");
        } else {
            uart_puts("[BOOT] WARNING: Copy may have failed - not a branch instruction\n");
        }
    } else {
        uart_puts("[BOOT] Vector table already valid at 0x89000, skipping copy\n");
    }
    
    // Invalidate and clean cache lines to ensure CPU sees the changes
    // Do this regardless of whether we copied or not
    uart_puts("[BOOT] Performing cache maintenance\n");
    asm volatile("dc cvau, %0" :: "r" (dst) : "memory");
    asm volatile("dsb ish");
    asm volatile("isb");
    
    uart_puts("[BOOT] Vector table ready at physical 0x89000\n");
}

// Function to dump memory contents for debugging
void dump_memory(const char* label, void* addr, int count) {
    unsigned char* p = (unsigned char*)addr;
    uart_puts(label);
    uart_puts(": ");
    
    for (int i = 0; i < count; i++) {
        uart_hex64(p[i]);
        uart_puts(" ");
    }
    uart_puts("\n");
}

// Function to verify physical memory at 0x89000
void verify_physical_vector_table(void) {
    uart_puts("[VERIFY] Contents at physical 0x89000:\n");
    
    // Read and display first 32 bytes at 0x89000
    unsigned char* addr = (unsigned char*)0x89000;
    uart_puts("Bytes: ");
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0 && i > 0) {
            uart_puts("\n       ");
        } else if (i > 0) {
            uart_puts(" ");
        }
        uart_puts("0x");
        uart_hex64(addr[i]);
    }
    uart_puts("\n");
    
    // Check first word - should be a branch instruction
    uint32_t first_word = *(uint32_t*)0x89000;
    uart_puts("First word: 0x");
    uart_hex64(first_word);
    uart_puts("\n");
    
    // Verify it's a branch instruction (most ARM64 branches start with 0x14)
    if ((first_word & 0xFC000000) == 0x14000000) {
        uart_puts("[VERIFY] First word looks like a valid ARM64 branch instruction\n");
    } else {
        uart_puts("[VERIFY] WARNING: First word doesn't look like a branch instruction\n");
    }
}

// Function to set VBAR_EL1 with proper debug output
void write_vbar_el1(uint64_t address) {
    uart_puts("[DEBUG] VBAR_EL1 set to ");
    uart_hex64(address);
    uart_puts("\n");
    
    // Set VBAR_EL1 to the specified address
    set_vbar_el1(address);
    
    // Read back VBAR_EL1 to verify it was set correctly
    uint64_t current_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    // Check if the set was successful
    if (current_vbar == address) {
        uart_puts("[DEBUG] VBAR_EL1 verified successfully\n");
    } else {
        uart_puts("[DEBUG] WARNING: VBAR_EL1 set failed! Got: ");
        uart_hex64(current_vbar);
        uart_puts("\n");
    }
}

// Function to validate vector table at physical address 0x89000
void validate_vector_table_at_0x89000(void) {
    volatile uint32_t* table = (volatile uint32_t*)0x89000;
    
    // ARM64 unconditional branch encoding: 0b000101xxxxxxxxxxxxxxxxxxxxxxxxx
    // Example branch encoding: 0x14000000 with different offsets
    if ((*table & 0xFC000000) == 0x14000000) {
        uart_puts("[BOOT] Vector table validated at 0x89000.\n");
    } else {
        uart_puts("[BOOT] ERROR: Vector table content invalid at 0x89000!\n");
        uart_puts("[BOOT] First word: ");
        uart_hex64(*table);
        uart_puts("\n");
    }
}

// Function to test exception handling
void test_exception_handling(void) {
    // All strings defined inline to avoid stale pointers
    uart_puts_late("\n[TEST] Testing exception handling\n");
    
    // Test SVC (Supervisor Call) exception
    uart_puts_late("[TEST] Triggering SVC #0 exception...\n");
    asm volatile ("svc #0");
    uart_puts_late("[TEST] Successfully returned from SVC #0\n");
    
    // Test SVC with different immediate value
    uart_puts_late("[TEST] Triggering SVC #1 exception...\n");
    asm volatile ("svc #1");
    uart_puts_late("[TEST] Successfully returned from SVC #1\n");
    
    uart_puts_late("[TEST] Exception handling test completed successfully\n");
}

// Test function to verify UART string output after MMU enablement
void test_uart_after_mmu(void) {
    // Define all strings inline to ensure they are allocated post-MMU
    // This prevents stale pointer issues from pre-MMU allocated strings
    
    // Use late functions explicitly
    uart_puts_late("\n[TEST] Testing UART output after MMU is enabled\n");
    
    // Long string test with inline definition
    uart_puts_late("[TEST] This is a longer string to test if UART string handling is working correctly after MMU is enabled\n");
    
    // Output some values using different methods
    uint64_t test_value = 0x1234567890ABCDEF;
    uart_puts_late("[TEST] Hex value: 0x");
    uart_hex64_late(test_value);
    uart_puts_late("\n");
    
    // Direct access test - explicitly use UART_VIRT
    volatile uint32_t* uart_dr = (volatile uint32_t*)UART_VIRT;
    *uart_dr = 'D';
    *uart_dr = 'I';
    *uart_dr = 'R';
    *uart_dr = 'E';
    *uart_dr = 'C';
    *uart_dr = 'T';
    *uart_dr = '\r';
    *uart_dr = '\n';
    
    // Test debug helper
    uart_puts_late("[TEST] Debug hex function test: ");
    uart_debug_hex(0xDEADBEEF);
    uart_puts_late("\n");
    
    // Final confirmation - inline string
    uart_puts_late("[TEST] UART test completed successfully\n\n");
}

// Entry point to the kernel. This gets called from start.S
__attribute__((used, externally_visible, noinline, section(".text.boot.main")))
void kernel_main(unsigned long uart_addr) {
    volatile unsigned int* uart = (unsigned int*)0x09000000;
    
    // Clear the UART output for a fresh start
    uart_clear_screen();
    
    // Output early stage marker 'A' directly to UART
    uart_debug_marker('A'); // Start of kernel_main
    
    // Initialize the UART for serial output - use early version explicitly
    uart_init_early(uart_addr);
    
    // Banner and version - explicitly use early functions before MMU
    uart_puts_early("\n\n===========[ CustomOS Kernel ]============\n");
    uart_puts_early("Version 0.1.0 - Boot Sequence\n");
    uart_puts_early("========================================\n\n");
    
    // Initialize memory management
    *uart = 'M';  // Memory initialization marker
    init_pmm();
    *uart = '1';  // PMM done marker
    uart_debug_marker('B'); // After PMM initialization
    
    // Validate vector table at physical address before MMU
    uart_puts_early("[BOOT] Validating vector table at physical address 0x89000\n");
    validate_vector_table_at_0x89000();
    
    // Improved vector table verification with clear markers
    uart_puts_early("\n\n=========== VECTOR TABLE VERIFICATION ===========\n");
    volatile uint8_t* p = (volatile uint8_t*)0x00089000;
    
    // Output first 32 bytes in a clean format
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0) {
            uart_puts_early("\n0x");
            uart_hex64_early((uint64_t)(p + i));
            uart_puts_early(": ");
        }
        
        char hi = (p[i] >> 4) & 0xF;
        char lo = p[i] & 0xF;
        uart_putc_early(hi < 10 ? '0' + hi : 'A' + (hi - 10));
        uart_putc_early(lo < 10 ? '0' + lo : 'A' + (lo - 10));
        uart_putc_early(' ');
    }
    uart_puts_early("\n\n=================================================\n\n");
    
    uart_debug_marker('C'); // After vector table verification
    
    // Perform cache maintenance for vector table
    uart_puts_early("[BOOT] Performing cache maintenance for vector table\n");
    asm volatile("dc cvau, %0" :: "r" (0x89000) : "memory");
    asm volatile("dsb ish");
    asm volatile("isb");
    
    // Set VBAR_EL1 to physical address BEFORE MMU
    uart_puts_early("[BOOT] Setting VBAR_EL1 to physical 0x89000 before MMU\n");
    write_vbar_el1(0x89000);
    uart_debug_marker('D'); // After setting VBAR_EL1 to 0x89000
    
    // Initialize VMM (sets up MMU)
    uart_puts_early("[BOOT] Initializing virtual memory management\n");
    uart_puts_early("[BOOT] MMU is about to be enabled\n");
    init_vmm_wrapper();
    // After this point, MMU is enabled and we should use uart_puts_late
    
    // After MMU is enabled, update VBAR_EL1 to virtual address
    // CRITICAL: Post-MMU code must use uart_puts_late
    uart_puts_late("[BOOT] Updating VBAR_EL1 to virtual 0x1000000 after MMU\n");
    write_vbar_el1(0x1000000);
    uart_debug_marker_late('F'); // After setting VBAR_EL1 to 0x1000000
    
    // Test UART string output after MMU enable
    // Use string literals directly inside this function to avoid stale pointer issues
    uart_puts_late("[BOOT] Testing UART string output after MMU is enabled\n");
    test_uart_after_mmu();
    uart_debug_marker_late('G'); // After UART MMU test
    
    // Run exception handling tests
    test_exception_handling();
    
    // Continue with initialization
    uart_puts_late("\n[BOOT] Continuing kernel initialization...\n");
}
