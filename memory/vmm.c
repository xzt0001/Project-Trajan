#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"

#define PAGE_SIZE           4096
#define PAGE_TABLE_ENTRIES  512  // 4KB / 8 bytes per entry

// Descriptor flags
#define PTE_VALID       (1UL << 0)
#define PTE_TABLE       (1UL << 1)
#define PTE_AF          (1UL << 10)  // Access Flag
#define PTE_SH_INNER    (3UL << 8)   // Inner shareable
#define PTE_AP_RW       (0UL << 6)   // Read/Write
#define PTE_PXN         (1UL << 53)  // Privileged eXecute Never
#define PTE_UXN         (1UL << 54)  // Unprivileged eXecute Never

// Memory attributes indexes
#define ATTR_NORMAL_IDX    0   // Attr0 in MAIR
#define ATTR_DEVICE_IDX    1   // Attr1 in MAIR

// Memory attributes encoded for page table entries
#define ATTR_NORMAL       (ATTR_NORMAL_IDX << 2)
#define ATTR_DEVICE       (ATTR_DEVICE_IDX << 2)

uint64_t* create_page_table(void) {
    void* table = alloc_page();
    if (!table) {
        uart_puts("[VMM] Failed to allocate page table!\n");
        return NULL;
    }

    memset(table, 0, PAGE_SIZE);  // clear entries
    return (uint64_t*)table;
}

void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags) {
    size_t index = (va >> 12) & 0x1FF;  // bits [20:12]
    l3_table[index] = (pa & ~0xFFFUL) | flags | PTE_VALID | PTE_AF | PTE_SH_INNER;
}

static uint64_t* l0_table = NULL;

// Define test addresses for MMU verification
#define TEST_PHYS_ADDR 0x1000000  // 16MB physical
#define TEST_VIRT_ADDR 0x2000000  // 32MB virtual
#define TEST_PATTERN   0xABCD1234DEADBEEF

// Kernel stack configuration
#define KERNEL_STACK_VA 0x400FF000  // Fixed virtual address for kernel stack
#define KERNEL_STACK_PATTERN 0xDEADBEEF

// Flag for MMU status
static int mmu_enabled = 0;

void init_vmm(void) {
    // No output here - handled in main.c
    
    // Reserve pages for page tables in advance (4 levels needed)
    reserve_pages_for_page_tables(8);
    
    l0_table = create_page_table();  // Level 0

    // Level 1 table
    uint64_t* l1 = create_page_table();
    l0_table[0] = ((uint64_t)l1 & ~0xFFFUL) | PTE_VALID | PTE_TABLE;

    // Level 2 table
    uint64_t* l2 = create_page_table();
    l1[0] = ((uint64_t)l2 & ~0xFFFUL) | PTE_VALID | PTE_TABLE;

    // Level 3 table
    uint64_t* l3 = create_page_table();
    l2[0] = ((uint64_t)l3 & ~0xFFFUL) | PTE_VALID | PTE_TABLE;

    // Identity-map kernel code (0x80000 - 0x400000)
    for (uint64_t addr = 0x80000; addr < 0x400000; addr += PAGE_SIZE) {
        map_page(l3, addr, addr, PTE_AP_RW | ATTR_NORMAL);
    }

    // Identity-map UART MMIO (QEMU PL011 at 0x09000000)
    map_page(l3, 0x09000000, 0x09000000, PTE_AP_RW | ATTR_DEVICE | PTE_PXN | PTE_UXN);
    
    // Map kernel stack and test pages
    void* stack_page = alloc_page();
    if (stack_page) {
        map_page(l3, KERNEL_STACK_VA, (uint64_t)stack_page, PTE_AP_RW | ATTR_NORMAL);
        volatile uint32_t* stack_ptr = (volatile uint32_t*)KERNEL_STACK_VA;
        *stack_ptr = KERNEL_STACK_PATTERN;
    }
    
    void* test_page = alloc_page();
    if (test_page) {
        map_page(l3, TEST_VIRT_ADDR, (uint64_t)test_page, PTE_AP_RW | ATTR_NORMAL);
        *(volatile uint64_t*)test_page = TEST_PATTERN;
    }
}

void enable_mmu(uint64_t* page_table_base) {
    // No output here - handled in main.c
    
    asm volatile (
        "dsb ish\n"

        // Set MAIR_EL1: 
        // Attr0 = Normal cacheable memory (0xFF)
        // Attr1 = Device-nGnRE memory (0x44)
        "mov x0, #0xFF\n"            // Normal memory in byte 0
        "movk x0, #0x44, lsl #16\n"  // Device memory in byte 2
        "msr mair_el1, x0\n"

        // Set TCR_EL1 for 4KB granule size, 48-bit VA
        // T0SZ=16 (48-bit), IPS=01 (36-bit PA)
        "mov x0, #0x19\n"             // T0SZ=16, TG0=0 (4KB)
        "movk x0, #0x1, lsl #32\n"    // IPS=01 (36-bit PA)
        "msr tcr_el1, x0\n"

        // Set TTBR0_EL1 (VA 0x0...) 
        "msr ttbr0_el1, %0\n"
        
        // Clear TTBR1_EL1
        "msr ttbr1_el1, xzr\n"

        "isb\n"

        // Enable MMU
        "mrs x0, sctlr_el1\n"
        "orr x0, x0, #1\n"           // M=1 (MMU)
        "msr sctlr_el1, x0\n"
        "isb\n"
        :
        : "r"(page_table_base)
        : "x0"
    );

    // MMU is now enabled
    mmu_enabled = 1;
}

uint64_t* get_kernel_page_table(void) {
    return l0_table;
}

// Check if MMU is currently enabled
int is_mmu_enabled(void) {
    return mmu_enabled;
}



