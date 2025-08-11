/*
 * vector_ops.c - Vector table management and validation
 * 
 * Provides utilities for copying, validating, and managing the ARM64 exception
 * vector table. The vector table contains the entry points for all exception
 * handlers and must be properly located and mapped for system stability.
 */

#include "../include/arch_ops.h"
#include "../include/console_api.h"

// Platform constants (TODO: move to platform config)
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// Vector table size (ARM64 standard)
#define VECTOR_TABLE_SIZE 0x800  // 2KB

// External symbols and functions
extern char vector_table[];
extern char _vector_table_load_start[];
extern uint64_t* get_kernel_page_table(void);
extern void ensure_vector_table_executable_l3(uint64_t* l3_table);
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);

/**
 * validate_vector_table_at_0x89000 - Validate vector table at physical address
 * 
 * Checks if the vector table at physical address 0x89000 contains valid
 * ARM64 branch instructions. Used to verify vector table copy operations
 * and ensure the table is ready for use.
 */
void validate_vector_table_at_0x89000(void) {
    volatile uint32_t* table = (volatile uint32_t*)0x89000;
    
    // ARM64 unconditional branch encoding: 0b000101xxxxxxxxxxxxxxxxxxxxxxxxx
    // Example branch encoding: 0x14000000 with different offsets
    if ((*table & 0xFC000000) == 0x14000000) {
        debug_print("[BOOT] Vector table validated at 0x89000.\n");
    } else {
        debug_print("[BOOT] ERROR: Vector table content invalid at 0x89000!\n");
        debug_print("[BOOT] First word: ");
        debug_hex64("", *table);
        debug_print("\n");
    }
}

/**
 * copy_vector_table_to_ram_if_needed - Copy vector table to physical RAM
 * 
 * Copies the vector table from its load address to the target physical
 * address (0x89000) if needed. Performs validation and cache maintenance
 * to ensure the copy is successful and visible to the CPU.
 */
void copy_vector_table_to_ram_if_needed(void) {
    char* src = _vector_table_load_start;    // Physical load address from linker
    char* dst = (char*)0x89000;              // Physical destination address
    size_t size = VECTOR_TABLE_SIZE;         // 2KB vector table size
    
    debug_print("[BOOT] Vector table copy check: LOAD_ADDR=");
    debug_hex64("", (uint64_t)src);
    debug_print(" DST=");
    debug_hex64("", (uint64_t)dst);
    debug_print("\n");
    
    // Check if source and destination are the same - if so, no copy needed
    if (src == dst) {
        debug_print("[BOOT] Vector table already at correct physical address, no copy needed\n");
        return;
    }
    
    // Simple validity check - only copy if destination appears to be zeroed or invalid
    volatile uint32_t* dst_word = (volatile uint32_t*)dst;
    if ((*dst_word & 0xFC000000) != 0x14000000) {
        debug_print("[BOOT] Vector table at 0x89000 doesn't contain valid branch instruction, copying...\n");
        
        // Copy 2KB (vector table size)
        for (size_t i = 0; i < size; i++) {
            dst[i] = src[i];
        }
        
        debug_print("[BOOT] Vector table copied to 0x89000\n");
        
        // Verify first instruction at destination (should be a branch)
        uint32_t first_instr = *(volatile uint32_t*)dst;
        debug_print("[BOOT] First word at destination: ");
        debug_hex64("", first_instr);
        debug_print("\n");
        
        // Verify it looks like an ARM64 branch instruction
        if ((first_instr & 0xFC000000) == 0x14000000) {
            debug_print("[BOOT] Copy successful - found valid ARM64 branch instruction\n");
        } else {
            debug_print("[BOOT] WARNING: Copy may have failed - not a branch instruction\n");
        }
    } else {
        debug_print("[BOOT] Vector table already valid at 0x89000, skipping copy\n");
    }
    
    // Invalidate and clean cache lines to ensure CPU sees the changes
    // Do this regardless of whether we copied or not
    debug_print("[BOOT] Performing cache maintenance\n");
    asm volatile("dc cvau, %0" :: "r" (dst) : "memory");
    asm volatile("dsb ish");
    asm volatile("isb");
    
    debug_print("[BOOT] Vector table ready at physical 0x89000\n");
}

/**
 * verify_and_fix_vector_table - Comprehensive vector table verification
 * 
 * Performs extensive verification of the vector table including:
 * - Address display and alignment checks
 * - Memory accessibility verification  
 * - Exception vector validation
 * - Page table mapping and executable permissions
 * 
 * Critical for ensuring exception handlers will work correctly.
 */
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

/**
 * verify_physical_vector_table - Verify vector table at physical 0x89000
 * 
 * Displays the contents of physical memory at 0x89000 to verify the
 * vector table was copied correctly. Shows both raw bytes and interprets
 * the first word as an ARM64 instruction.
 */
void verify_physical_vector_table(void) {
    debug_print("[VERIFY] Contents at physical 0x89000:\n");
    
    // Read and display first 32 bytes at 0x89000
    unsigned char* addr = (unsigned char*)0x89000;
    debug_print("Bytes: ");
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0 && i > 0) {
            debug_print("\n       ");
        } else if (i > 0) {
            debug_print(" ");
        }
        debug_print("0x");
        
        // Print byte in hex
        uint8_t byte = addr[i];
        uint8_t hi = (byte >> 4) & 0xF;
        uint8_t lo = byte & 0xF;
        
        volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
        *uart = hi < 10 ? '0' + hi : 'A' + (hi - 10);
        *uart = lo < 10 ? '0' + lo : 'A' + (lo - 10);
    }
    debug_print("\n");
    
    // Check first word - should be a branch instruction
    uint32_t first_word = *(uint32_t*)0x89000;
    debug_print("First word: 0x");
    debug_hex64("", first_word);
    debug_print("\n");
    
    // Verify it's a branch instruction (most ARM64 branches start with 0x14)
    if ((first_word & 0xFC000000) == 0x14000000) {
        debug_print("[VERIFY] First word looks like a valid ARM64 branch instruction\n");
    } else {
        debug_print("[VERIFY] WARNING: First word doesn't look like a branch instruction\n");
    }
}
