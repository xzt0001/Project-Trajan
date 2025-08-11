/*
 * memory_debug.h - Memory debugging and introspection API
 * 
 * Provides utilities for debugging memory management, page table structure,
 * and virtual-to-physical address translation during kernel development.
 */

#ifndef KERNEL_INIT_MEMORY_DEBUG_H
#define KERNEL_INIT_MEMORY_DEBUG_H

#include "../../../include/types.h"

/* ========== Page Table Analysis Functions ========== */

/**
 * decode_pte - Decode and display page table entry flags
 * @pte: Page table entry to decode
 * 
 * Decodes a 64-bit page table entry and displays all relevant flags
 * in a compact, human-readable format. Essential for debugging
 * translation permission issues.
 * 
 * Example output: "FLAGS: VTWNApu0"
 * - V/v = Valid/invalid
 * - T/B = Table/Block entry
 * - W/R/w/r = Access permissions  
 * - N/O/I/S = Shareability
 * - A/a = Access flag
 * - P/p = PXN flag
 * - U/u = UXN flag
 * - 0-7 = Memory attribute index
 */
void decode_pte(uint64_t pte);

/**
 * dump_page_mapping - Complete page table walk and analysis
 * @label: Descriptive label for this mapping dump
 * @virt_addr: Virtual address to trace through page tables
 * 
 * Performs a comprehensive 4-level page table walk (L0->L1->L2->L3)
 * displaying page table indices, addresses, entries, and final
 * physical mapping. Essential for debugging virtual memory setup.
 * 
 * Output includes:
 * - Page table indices at each level (in binary)
 * - Page table addresses at each level
 * - Page table entries at each level
 * - Final PTE flag analysis
 * - Resulting physical address
 */
void dump_page_mapping(const char* label, uint64_t virt_addr);

/* ========== Memory Content Analysis ========== */

/**
 * dump_memory - Display memory contents in hex format
 * @label: Descriptive label for the memory dump  
 * @addr: Starting address to dump
 * @count: Number of bytes to display
 * 
 * Displays raw memory contents in hexadecimal format.
 * Useful for examining data structures and debugging corruption.
 * 
 * Example: "Vector Table: 0x14 0x00 0x00 0x00 ..."
 */
void dump_memory(const char* label, void* addr, int count);

/* ========== Future Extensions ========== */

/*
 * TODO: Add more memory debugging utilities:
 * - dump_page_table_range() - Dump multiple consecutive entries
 * - analyze_memory_layout() - High-level memory map analysis  
 * - check_mapping_consistency() - Verify mapping integrity
 * - trace_memory_access() - Log memory access patterns
 */

#endif /* KERNEL_INIT_MEMORY_DEBUG_H */
