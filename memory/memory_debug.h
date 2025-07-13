#ifndef MEMORY_DEBUG_H
#define MEMORY_DEBUG_H

#include "../include/types.h"

// Function prototypes for memory debugging functions

/**
 * Dump PTE info in a simpler format for debugging
 * @param vaddr Virtual address to dump PTE for
 */
void debug_dump_pte(uint64_t vaddr);

/**
 * Print page table entry flags for debugging
 * @param va Virtual address to analyze
 */
void print_pte_flags(uint64_t va);

/**
 * Debug function to check mapping for a specific address
 * @param addr Address to check
 * @param name Name/description of the address for logging
 */
void debug_check_mapping(uint64_t addr, const char* name);

/**
 * Verify an address is properly mapped as executable through all page table levels
 * @param table_ptr Page table pointer
 * @param vaddr Virtual address to verify
 * @param desc Description for logging
 * @return 1 if address is executable, 0 otherwise
 */
int verify_executable_address(uint64_t *table_ptr, uint64_t vaddr, const char* desc);

/**
 * Enhanced page table verification and auto-fix before MMU enable
 * @param page_table_base Base address of page table
 */
void verify_critical_mappings_before_mmu(uint64_t* page_table_base);

/**
 * Audit memory mappings for debugging purposes
 */
void audit_memory_mappings(void);

/**
 * Verify code sections are executable
 */
void verify_code_is_executable(void);

/**
 * Debug function to print text section info
 */
void print_text_section_info(void);

/**
 * Register a memory mapping for diagnostic purposes
 * @param virt_start Virtual start address
 * @param virt_end Virtual end address
 * @param phys_start Physical start address
 * @param flags Page table flags
 * @param name Name/description of the mapping
 */
void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, uint64_t flags, const char* name);

/**
 * Helper function to flush cache lines for a memory region
 * @param addr Starting address
 * @param size Size of the region
 */
void flush_cache_lines(void* addr, size_t size);

#endif // MEMORY_DEBUG_H 