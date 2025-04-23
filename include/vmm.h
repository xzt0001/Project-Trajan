#ifndef VMM_H
#define VMM_H

#include "types.h"

// Create a zeroed-out page table (used internally)
uint64_t* create_page_table(void);

// Map a virtual address to a physical address with flags
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);

// Map a page in the kernel address space (convenience wrapper)
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t flags);

// Map the user task section with EL0 permissions
void map_user_task_section(void);

// Initialize the full kernel page table (L0 → L3 + kernel mappings)
void init_vmm(void);

// Activate the MMU using the given top-level page table
void enable_mmu(uint64_t* page_table_base);

// Accessor for the kernel's L0 page table (used in main.c)
uint64_t* get_kernel_page_table(void);

// Check if MMU is currently enabled
int is_mmu_enabled(void);

#endif /* VMM_H */
