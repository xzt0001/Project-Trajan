/*
 * vbar_ops.c - VBAR_EL1 management and vector base register operations
 * 
 * Provides utilities for managing the Vector Base Address Register (VBAR_EL1)
 * which controls where the CPU looks for exception vector handlers. Critical
 * for proper exception handling and system stability.
 */

#include "../include/arch_ops.h"
#include "../include/console_api.h"

// Platform constants (TODO: move to platform config)
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// Vector table symbols from linker script
extern char vector_table[];
extern void set_vbar_el1(unsigned long addr);

/**
 * verify_vbar_el1 - Verify VBAR_EL1 is correctly set
 * 
 * Reads the current VBAR_EL1 value and compares it against the expected
 * vector table address. Performs alignment checks and memory accessibility
 * verification. Essential for diagnosing exception handler issues.
 */
void verify_vbar_el1(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = '_'; 
    *uart = 'C'; *uart = 'H'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
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
        
        // Check alignment (must be 2KB aligned)
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

/**
 * ensure_vbar_el1 - Check and restore VBAR_EL1 if needed
 * 
 * Defensive function that verifies VBAR_EL1 hasn't been corrupted
 * and restores it to the correct value if necessary. Can be called
 * periodically to detect and recover from VBAR corruption.
 */
void ensure_vbar_el1(void) {
    uint64_t current_vbar;
    uint64_t expected_vbar = (uint64_t)vector_table;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    debug_print("[VBAR] Checking VBAR_EL1...\n");
    debug_print("[VBAR] Current:  0x");
    debug_hex64("", current_vbar);
    debug_print("[VBAR] Expected: 0x");
    debug_hex64("", expected_vbar);
    debug_print("\n");
    
    if (current_vbar != expected_vbar) {
        debug_print("[VBAR] CRITICAL: VBAR_EL1 was changed! Restoring...\n");
        // Reset it to the correct value
        asm volatile("msr vbar_el1, %0" :: "r"(expected_vbar));
        asm volatile("isb");
        
        // Verify it was properly set
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        debug_print("[VBAR] After reset: 0x");
        debug_hex64("", current_vbar);
        debug_print("\n");
    } else {
        debug_print("[VBAR] VBAR_EL1 is correctly set\n");
    }
}

/**
 * write_vbar_el1 - Set VBAR_EL1 with verification
 * @address: Physical or virtual address to set as vector base
 * 
 * Sets VBAR_EL1 to the specified address and verifies the operation
 * succeeded. Provides debug output for visibility during boot.
 * Used for both physical (pre-MMU) and virtual (post-MMU) addresses.
 */
void write_vbar_el1(uint64_t address) {
    debug_print("[DEBUG] VBAR_EL1 set to ");
    debug_hex64("", address);
    debug_print("\n");
    
    // Set VBAR_EL1 to the specified address
    set_vbar_el1(address);
    
    // Read back VBAR_EL1 to verify it was set correctly
    uint64_t current_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    // Check if the set was successful
    if (current_vbar == address) {
        debug_print("[DEBUG] VBAR_EL1 verified successfully\n");
    } else {
        debug_print("[DEBUG] WARNING: VBAR_EL1 set failed! Got: ");
        debug_hex64("", current_vbar);
        debug_print("\n");
    }
}

/**
 * init_traps - Initialize trap handlers with virtual addressing
 * 
 * Sets up VBAR_EL1 for virtual memory mode, using the mapped vector
 * table address. Called after MMU initialization to transition from
 * physical to virtual exception handling.
 */
void init_traps(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
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
    debug_print("[VBAR] Setting vector table to: 0x");
    debug_hex64("", vt_addr);
    debug_print("\n");
    
    // Set VBAR_EL1 to the mapped virtual address
    asm volatile("msr vbar_el1, %0" :: "r"(vt_addr));
    asm volatile("isb");
    
    // Verify value was set
    uint64_t vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    
    // Add verification code
    debug_print("[VBAR] Set VBAR_EL1 to 0x1000000, read back 0x");
    debug_hex64("", vbar);
    debug_print("\n");
    
    // Verify VBAR_EL1 is set to 0x1000000
    if (vbar != 0x1000000) {
        debug_print("[VBAR] ERROR: VBAR_EL1 not set to 0x1000000! Current value: 0x");
        debug_hex64("", vbar);
        debug_print("\n");
        
        // Try setting it again
        debug_print("[VBAR] Attempting to set VBAR_EL1 one more time...\n");
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(vt_addr)
        );
        
        // Check again
        asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
        debug_print("[VBAR] After second attempt: 0x");
        debug_hex64("", vbar);
        debug_print("\n");
    } else {
        debug_print("[VBAR] SUCCESS: VBAR_EL1 correctly set to 0x1000000\n");
    }
}

/**
 * init_exceptions_minimal - Minimal exception initialization
 * 
 * Simplified exception setup for early boot. Sets VBAR_EL1 with
 * minimal code and hangs on any errors. Used when full debug
 * infrastructure isn't available yet.
 */
void init_exceptions_minimal(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
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
