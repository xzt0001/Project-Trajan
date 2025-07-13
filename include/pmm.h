#ifndef PMM_H
#define PMM_H

#include "types.h"

// Physical memory manager functions
void init_pmm(void);
void* alloc_page(void);
void free_page(void* addr);
void reserve_pages_for_page_tables(uint64_t num_pages);

// Physical memory mapping functions (moved from vmm.c in Phase 4)
// NOTE: write_phys64 is implemented as a static inline function in memory_config.h
uint64_t* create_page_table(void);
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
               uint64_t phys_start, uint64_t flags);
void map_page_direct(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t flags);
void map_uart(void);
void verify_uart_mapping(void);

#endif /* PMM_H */
