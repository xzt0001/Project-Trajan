#ifndef VMM_H
#define VMM_H

#include "types.h"
#include "uart.h"

// Debug mode for VMM operations
#define DEBUG_VMM_MODE 1

// Use UART_VIRT from uart.h instead of redefining it
// #define UART_VIRT           0xFFFF000009000000

// Page table entry flags
#define PTE_VALID       (1UL << 0)  // Entry is valid
#define PTE_TABLE       (1UL << 1)  // Entry points to another table (vs block/page)
#define PTE_PAGE        (3UL << 0)  // Combination of VALID and leaf page
#define PTE_AF          (1UL << 10) // Access flag - set when page is accessed
#define PTE_SH_INNER    (3UL << 8)  // Shareability: Inner Shareable (multi-core cache)
#define PTE_AP_RW       (0UL << 6)  // Access permissions: Read/Write at any EL
#define PTE_UXN         (1UL << 54) // Unprivileged Execute Never (user can't execute)
#define PTE_PXN         (1UL << 53) // Privileged Execute Never (kernel can't execute)

// Memory attribute indices
#define ATTR_NORMAL_IDX 0           // Index for normal memory attributes
#define ATTR_DEVICE_IDX 1           // Index for device memory attributes
#define ATTR_NORMAL     (ATTR_NORMAL_IDX << 2)  // Normal memory attribute
#define ATTR_DEVICE     (ATTR_DEVICE_IDX << 2)  // Device memory attribute 

// Create a zeroed-out page table (used internally)
uint64_t* create_page_table(void);

// Map a virtual address to a physical address with flags
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);

// Map a page in the kernel address space (convenience wrapper)
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t flags);

// Map a range of pages
void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
               uint64_t phys_start, uint64_t flags);

// Map the user task section with EL0 permissions
void map_user_task_section(void);

// Map kernel sections (.text, .rodata, .data, .bss)
void map_kernel_sections(void);

// Initialize the full kernel page table (L0 → L3 + kernel mappings)
void init_vmm(void);

// Activate the MMU using the given top-level page table
void enable_mmu(uint64_t* page_table_base);

// Accessor for the kernel's L0 page table (used in main.c)
uint64_t* get_kernel_page_table(void);

// Check if MMU is currently enabled
int is_mmu_enabled(void);

// Get page table entry for a virtual address
uint64_t get_pte(uint64_t virt_addr);

// UART mapping and verification functions
void map_uart(void);
void verify_uart_mapping(void);

// Phase 9: Memory mapping audit functions
void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, uint64_t flags, const char* name);
bool regions_overlap(uint64_t start1, uint64_t end1, uint64_t start2, uint64_t end2);
void audit_memory_mappings(void);

// UART addresses
#define UART_PHYS           0x09000000

#endif /* VMM_H */
