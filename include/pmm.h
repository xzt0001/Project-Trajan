#ifndef PMM_H
#define PMM_H

#include "types.h"

// Physical memory manager functions
void init_pmm(void);
void* alloc_page(void);
void free_page(void* addr);
void reserve_pages_for_page_tables(uint64_t num_pages);

#endif /* PMM_H */
