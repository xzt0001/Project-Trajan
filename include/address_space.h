#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include "types.h"
#include "memory_config.h"

// Unified memory management interface
int init_memory_subsystem(void);
void* addr_alloc_page(void);
int addr_free_page(void* addr);
int addr_map_device(uint64_t phys_addr, uint64_t virt_addr, uint64_t size, uint64_t flags);
int addr_map_range(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, uint64_t flags);
int addr_unmap_page(uint64_t virt_addr);

// Memory subsystem status
bool is_mmu_enabled(void);
bool is_virtual_addressing_available(void);

#endif // ADDRESS_SPACE_H 