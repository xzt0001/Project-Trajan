#ifndef MEMORY_CORE_H
#define MEMORY_CORE_H

#include "types.h"

// System register access functions
uint64_t read_ttbr1_el1(void);
uint64_t read_vbar_el1(void);
uint64_t read_mair_el1(void);

// Cache and MMU operations
void enhanced_cache_maintenance(void);
void enable_mmu_enhanced(uint64_t* page_table_base);
void map_vector_table_dual(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1, 
                           uint64_t vector_addr);

// Page table management
uint64_t* init_page_tables(void);
uint64_t* get_kernel_page_table(void);
uint64_t* get_kernel_ttbr1_page_table(void);
uint64_t* get_kernel_l3_table(void);

#endif // MEMORY_CORE_H 