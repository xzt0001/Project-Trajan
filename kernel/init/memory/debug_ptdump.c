/*
 * debug_ptdump.c - Memory debugging and page table introspection
 * 
 * Provides utilities for debugging memory management, page table structure,
 * and virtual-to-physical address translation. These functions are essential
 * for diagnosing MMU and memory mapping issues during kernel development.
 */

#include "../include/memory_debug.h"
#include "../include/console_api.h"

// Platform-specific constants (TODO: move to platform config)
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// Page table flags (duplicated from main.c - TODO: centralize)
#define PTE_VALID       (1UL << 0)  // Entry is valid

// External VMM functions
extern uint64_t* get_kernel_page_table(void);

/**
 * decode_pte - Decode and display page table entry flags
 * @pte: Page table entry to decode
 * 
 * Decodes a 64-bit page table entry and displays all relevant flags
 * in a compact, human-readable format. Useful for debugging translation
 * issues and permission problems.
 * 
 * Output format: FLAGS: VTWNApu<attr>
 * Where:
 *   V/v = Valid/invalid
 *   T/B = Table/Block entry  
 *   W/R/w/r = Access permissions (RW all/RO all/RW EL1/RO EL1)
 *   N/O/I/S = Shareability (None/Outer/Inner/Reserved)
 *   A/a = Access flag set/clear
 *   P/p = PXN (Privileged Execute Never) set/clear
 *   U/u = UXN (Unprivileged Execute Never) set/clear
 *   <attr> = Memory attribute index (0-7)
 */
void decode_pte(uint64_t pte) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
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

/**
 * dump_page_mapping - Comprehensive page table walk and analysis
 * @label: Descriptive label for this mapping dump
 * @virt_addr: Virtual address to trace through page tables
 * 
 * Performs a complete 4-level page table walk (L0->L1->L2->L3) for the given
 * virtual address, displaying:
 * - Page table indices at each level
 * - Page table addresses at each level  
 * - Page table entries at each level
 * - Final PTE flag decoding
 * - Resulting physical address
 * 
 * Essential for debugging virtual memory setup and translation issues.
 */
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
    
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    // Print indices in binary for clarity
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

/**
 * dump_memory - Display memory contents in hex format
 * @label: Descriptive label for the memory dump
 * @addr: Starting address to dump
 * @count: Number of bytes to display
 * 
 * Displays raw memory contents in hexadecimal format. Useful for
 * examining data structures, verifying memory initialization,
 * and debugging data corruption issues.
 * 
 * Output format: "<label>: 0x<byte1> 0x<byte2> ... \n"
 */
void dump_memory(const char* label, void* addr, int count) {
    unsigned char* p = (unsigned char*)addr;
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    debug_print(label);
    debug_print(": ");
    
    for (int i = 0; i < count; i++) {
        // Print each byte in hex format manually
        *uart = '0'; *uart = 'x';
        
        uint8_t byte = p[i];
        uint8_t hi = (byte >> 4) & 0xF;
        uint8_t lo = byte & 0xF;
        
        *uart = hi < 10 ? '0' + hi : 'A' + (hi - 10);
        *uart = lo < 10 ? '0' + lo : 'A' + (lo - 10);
        *uart = ' ';
    }
    debug_print("\n");
}
