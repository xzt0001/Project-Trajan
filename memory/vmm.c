#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"

// Define required constants at the top of the file
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1 << PAGE_SHIFT)  // 4096 bytes
#define ENTRIES_PER_TABLE 512

// Define bool type since we can't include stdbool.h
typedef int bool;
#define true 1
#define false 0

// Prototypes for diagnostic helpers
uint64_t read_mair_el1(void);
uint64_t read_pte_entry(uint64_t va);
void debug_hex64_mmu(const char* label, uint64_t value);

// Structure to hold both virtual and physical addresses for page tables
typedef struct {
    uint64_t* virt;
    uint64_t  phys;
} PageTableRef;

// Global variables
extern bool mmu_enabled;  // Use the one defined in uart_late.c
static uint64_t* l0_table = NULL;
// Make this variable accessible from other files
uint64_t saved_vector_table_addr = 0; // Added to preserve vector table address

// Debug flag - define at the top before it's used
static bool debug_vmm = false; // Set to false to reduce UART noise during boot

// Forward declarations for UART handling
void map_uart(void);
void verify_uart_mapping(void);

// Function to safely write to physical memory with proper cache maintenance
static inline void write_phys64(uint64_t phys_addr, uint64_t value) {
    volatile uint64_t* addr = (volatile uint64_t*)phys_addr;
    *addr = value;
    
    // Data Cache Clean by VA to Point of Coherency
    asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
    
    // Full system Data Synchronization Barrier
    asm volatile("dsb sy" ::: "memory");
    
    // Instruction Synchronization Barrier
    asm volatile("isb" ::: "memory");
}

// Define constants for ARMv8 page table entries
#define PTE_VALID       (1UL << 0)   // Entry is valid
#define PTE_TABLE       (1UL << 1)   // Entry points to another table
#define PTE_PAGE        (0UL << 1)   // Entry points to a page (NOT a table)
#define PTE_AF          (1UL << 10)  // Access Flag - must be set to avoid access faults

// Memory attributes indices for MAIR register
// These define the cacheability/shareability of memory regions
#define ATTR_IDX_DEVICE_nGnRnE 0  // Device, non-Gathering, non-Reordering, no Early write ack
#define ATTR_IDX_NORMAL       1  // Normal memory, Inner/Outer Write-Back Cacheable
#define ATTR_IDX_NORMAL_NC    2  // Normal memory, non-cacheable
#define ATTR_IDX_DEVICE_nGnRE 3  // Device, non-Gathering, non-Reordering, Early write ack

// ARMv8 Memory Region Attributes (used in MAIR_EL1 register)
#define MAIR_ATTR_DEVICE_nGnRnE 0x00  // Device: non-Gathering, non-Reordering, non-Early Write Ack
#define MAIR_ATTR_DEVICE_nGnRE  0x04  // Device: non-Gathering, non-Reordering, Early Write Ack
#define MAIR_ATTR_NORMAL_NC     0x44  // Normal Memory: NC, NC
#define MAIR_ATTR_NORMAL        0xFF  // Normal Memory: WB RA/WA, WB RA/WA

// Complete Memory Type Attributes for ARMv8
#define PTE_ATTRINDX(idx)   ((idx) << 2)  // Shift attribute index to appropriate bits [4:2]
#define PTE_NORMAL          PTE_ATTRINDX(ATTR_IDX_NORMAL)
#define PTE_NORMAL_NC       PTE_ATTRINDX(ATTR_IDX_NORMAL_NC)
#define PTE_DEVICE_nGnRnE   PTE_ATTRINDX(ATTR_IDX_DEVICE_nGnRnE)
#define PTE_DEVICE_nGnRE    PTE_ATTRINDX(ATTR_IDX_DEVICE_nGnRE)

// Access Permissions
#define PTE_AP_RW       (0UL << 6)   // Read-Write for EL1, no access for EL0
#define PTE_AP_RO       (1UL << 6)   // Read-Only for EL1, no access for EL0
#define PTE_AP_RW_EL0   (1UL << 7 | 0UL << 6)   // Read-Write for EL1 and EL0
#define PTE_AP_RO_EL0   (1UL << 7 | 1UL << 6)   // Read-Only for EL1 and EL0
#define PTE_AP_USER     (1UL << 7)   // Add EL0 access when set - for backward compatibility
#define PTE_AP_MASK     (3UL << 6)   // Access Permission mask (bits 6-7)

// Execute permissions - Execute Never bits
#define PTE_UXN         (1UL << 54)  // Unprivileged Execute Never (EL0 can't execute)
#define PTE_PXN         (1UL << 53)  // Privileged Execute Never (EL1 can't execute)
#define PTE_NOEXEC      (PTE_UXN | PTE_PXN)  // No execution at any level

// Shareability attributes
#define PTE_SH_NONE     (0UL << 8)   // Non-shareable
#define PTE_SH_OUTER    (2UL << 8)   // Outer shareable
#define PTE_SH_INNER    (3UL << 8)   // Inner shareable

// Address masks
#define PTE_TABLE_ADDR  (~0xFFFUL)   // Address mask for table entries (bits [47:12])
#define PTE_ADDR_MASK   (~0xFFFUL)   // Physical address mask for page table entries (bits [47:12])

// Combined flags for typical memory regions
#define PTE_KERN_DATA   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW | PTE_NOEXEC)
#define PTE_KERN_RODATA (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO | PTE_NOEXEC)
#define PTE_KERN_TEXT   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO)

#define PTE_USER_DATA   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW_EL0 | PTE_NOEXEC)
#define PTE_USER_RODATA (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO_EL0 | PTE_NOEXEC)
#define PTE_USER_TEXT   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO_EL0 | PTE_UXN)

// Additional flag combinations for MMIO regions
#define PTE_DEVICE      (PTE_VALID | PTE_AF | PTE_SH_OUTER | PTE_DEVICE_nGnRnE | PTE_AP_RW | PTE_NOEXEC)

// Debug UART for direct output even when system is unstable
#define DEBUG_UART 0x09000000

// Additional definitions for backward compatibility
#define PTE_KERNEL_EXEC (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW)  // Kernel executable memory
#define PTE_EXEC        (0UL)       // Executable at all levels (both UXN/PXN clear)
#define ATTR_NORMAL_EXEC  PTE_NORMAL  // Normal memory with execution permitted
#define PTE_ACCESS      PTE_AF      // Simplified alias for the Access Flag

// Explicit inverse flags for readability
#define PTE_PXN_DISABLE (0UL)  // Allow privilege execution (kernel can execute)
#define PTE_UXN_DISABLE (0UL)  // Allow unprivileged execution (user can execute)

// Function prototype declarations 
void verify_kernel_executable(void);
void ensure_vector_table_executable(void);
void ensure_vector_table_executable_l3(uint64_t* l3_table);
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
void enable_mmu(uint64_t* page_table_base);
void enable_mmu_enhanced(uint64_t* page_table_base);
uint64_t read_ttbr1_el1(void);
uint64_t read_vbar_el1(void);
void map_vector_table(void);  // Add forward declaration
uint64_t* get_kernel_l3_table(void);  // Add forward declaration
void verify_page_mapping(uint64_t va);  // Add forward declaration
uint64_t* init_page_tables(void);  // Add forward declaration for the page table initialization

// Add missing function declarations
extern void uart_putx(uint64_t val);
extern void uart_hex64(uint64_t val);

// MSR/MRS macros for system register access
#define MSR(reg, val) __asm__ volatile("msr " reg ", %0" :: "r" (val))
#define MRS(reg, val) __asm__ volatile("mrs %0, " reg : "=r" (val))

// Add missing function declarations
extern void debug_print(const char* msg);
extern void debug_hex64(const char* label, uint64_t value);
extern void uart_putc(char c);

// Forward declaration for MMU continuation function
void __attribute__((used, aligned(4096))) mmu_continuation_point(void);
void flush_cache_lines(void* addr, size_t size);

// Add missing attribute index definitions at the top of the file with other defines
#define ATTR_IDX_DEVICE_nGnRnE 0
#define ATTR_IDX_NORMAL 1
#define ATTR_IDX_NORMAL_NC 2
#define ATTR_IDX_DEVICE_nGnRE  3  // Device non-Gathering, non-Reordering, Early ack

// Add kernel base definition
// #define KERNEL_BASE 0xFFFF000000000000UL

// Helper functions to read system registers
uint64_t read_ttbr1_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(val));
    return val;
}

uint64_t read_vbar_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(val));
    return val;
}

// Function to dump PTE info in a simpler format for debugging
void debug_dump_pte(uint64_t vaddr) {
    // Use existing functions to locate the page table entry
    uint64_t* l0 = get_kernel_page_table();
    if (!l0) {
        uart_puts("ERROR: No kernel page table available!\n");
        return;
    }
    
    // Walk the page table to get the PTE for this VA
    uint64_t l0_idx = (vaddr >> 39) & 0x1FF;
    uint64_t l1_idx = (vaddr >> 30) & 0x1FF;
    uint64_t l2_idx = (vaddr >> 21) & 0x1FF;
    uint64_t l3_idx = (vaddr >> 12) & 0x1FF;
    
    uart_puts("## PTE for 0x");
    uart_hex64(vaddr);
    uart_puts(":\n");
    
    // Check L0 entry
    if (!(l0[l0_idx] & PTE_VALID)) {
        uart_puts("  L0 invalid\n");
        return;
    }
    
    // Get L1 table
    uint64_t* l1 = (uint64_t*)((l0[l0_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L1 entry
    if (!(l1[l1_idx] & PTE_VALID)) {
        uart_puts("  L1 invalid\n");
        return;
    }
    
    // Get L2 table
    uint64_t* l2 = (uint64_t*)((l1[l1_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L2 entry
    if (!(l2[l2_idx] & PTE_VALID)) {
        uart_puts("  L2 invalid\n");
        return;
    }
    
    // Get L3 table
    uint64_t* l3 = (uint64_t*)((l2[l2_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L3 entry (final PTE)
    if (!(l3[l3_idx] & PTE_VALID)) {
        uart_puts("  L3 invalid\n");
        return;
    }
    
    // This is the actual page table entry for our virtual address
    uint64_t pte = l3[l3_idx];
    
    uart_puts("  Raw: 0x");
    uart_hex64(pte);
    uart_puts("\n");
}

// Function to print page table entry flags for debugging
void print_pte_flags(uint64_t va) {
    // Get kernel page table instead of using undefined L1_PAGE_TABLE_BASE
    // FIXED: Use a different variable name to avoid shadowing
    uint64_t* l0_pt_local = get_kernel_page_table();
    if (!l0_pt_local) {
        uart_puts("ERROR: No kernel page table available!\n");
        return;
    }
    
    // Walk the page table to get the PTE for this VA
    uint64_t l0_idx = (va >> 39) & 0x1FF;
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;
    
    uart_puts("Page table walk for VA: ");
    uart_hex64(va);
    uart_putc('\n');
    
    // Check L0 entry
    if (!(l0_pt_local[l0_idx] & PTE_VALID)) {
        uart_puts("  L0 entry invalid!\n");
        return;
    }
    
    uart_puts("  L0 entry: ");
    uart_hex64(l0_pt_local[l0_idx]);
    uart_putc('\n');
    
    // Get L1 table
    uint64_t* l1_table = (uint64_t*)((l0_pt_local[l0_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L1 entry
    if (!(l1_table[l1_idx] & PTE_VALID)) {
        uart_puts("  L1 entry invalid!\n");
        return;
    }
    
    uart_puts("  L1 entry: ");
    uart_hex64(l1_table[l1_idx]);
    uart_putc('\n');
    
    // Get L2 table
    uint64_t* l2_table = (uint64_t*)((l1_table[l1_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L2 entry
    if (!(l2_table[l2_idx] & PTE_VALID)) {
        uart_puts("  L2 entry invalid!\n");
        return;
    }
    
    uart_puts("  L2 entry: ");
    uart_hex64(l2_table[l2_idx]);
    uart_putc('\n');
    
    // Get L3 table
    uint64_t* l3_table = (uint64_t*)((l2_table[l2_idx] & ~0xFFF) & ~PTE_TABLE);
    
    // Check L3 entry (final PTE)
    if (!(l3_table[l3_idx] & PTE_VALID)) {
        uart_puts("  L3 entry invalid!\n");
        return;
    }
    
    // This is the actual page table entry for our virtual address
    uint64_t pte = l3_table[l3_idx];
    
    uart_puts("  L3 entry (PTE): ");
    uart_hex64(pte);
    uart_putc('\n');
    
    // Check key flags
    uart_puts("  Valid: ");
    uart_putc((pte & PTE_VALID) ? '1' : '0');
    uart_putc('\n');
    
    uart_puts("  PXN: ");
    uart_putc((pte & PTE_PXN) ? '1' : '0');
    uart_putc('\n');
    
    uart_puts("  UXN: ");
    uart_putc((pte & PTE_UXN) ? '1' : '0');
    uart_putc('\n');
    
    uart_puts("  AF: ");
    uart_putc((pte & PTE_AF) ? '1' : '0');
    uart_putc('\n');
    
    uart_puts("  AttrIdx: ");
    uart_hex64((pte >> 2) & 0x7);
    uart_putc('\n');
}

// Fix the debug_check_mapping function to accept a name parameter and correct usage
void debug_check_mapping(uint64_t addr, const char* name) {
    uart_puts("[DEBUG] Checking mapping for ");
    uart_puts(name);
    uart_puts(" at 0x");
    uart_hex64(addr);
    uart_puts("\n");
    
    // Get the kernel page table root
    // FIXED: Use a different variable name to avoid shadowing
    uint64_t* l0_pt_local = get_kernel_page_table();
    if (!l0_pt_local) {
        uart_puts("ERROR: Kernel page table not initialized!\n");
        return;
    }
    
    uint64_t l0_idx = (addr >> 39) & 0x1FF;
    uint64_t l1_idx = (addr >> 30) & 0x1FF;
    uint64_t l2_idx = (addr >> 21) & 0x1FF;
    uint64_t l3_idx = (addr >> 12) & 0x1FF;
    
    uart_puts("  L0 index: ");
    uart_hex64(l0_idx);
    uart_puts("\n");
    
    // Check if L0 entry is valid
    if (!(l0_pt_local[l0_idx] & PTE_VALID)) {
        uart_puts("  L0 entry not valid!\n");
        return;
    }
    
    // Get L1 table address
    uint64_t* l1_table = (uint64_t*)((l0_pt_local[l0_idx] & ~0xFFFUL) & ~PTE_TABLE);
    uart_puts("  L1 table at: 0x");
    uart_hex64((uint64_t)l1_table);
    uart_puts("\n");
    
    // Check if L1 entry is valid
    if (!(l1_table[l1_idx] & PTE_VALID)) {
        uart_puts("  L1 entry not valid!\n");
        return;
    }
    
    // Get L2 table address
    uint64_t* l2_table = (uint64_t*)((l1_table[l1_idx] & ~0xFFFUL) & ~PTE_TABLE);
    uart_puts("  L2 table at: 0x");
    uart_hex64((uint64_t)l2_table);
    uart_puts("\n");
    
    // Check if L2 entry is valid
    if (!(l2_table[l2_idx] & PTE_VALID)) {
        uart_puts("  L2 entry not valid!\n");
        return;
    }
    
    // Get L3 table address
    uint64_t* l3_table = (uint64_t*)((l2_table[l2_idx] & ~0xFFFUL) & ~PTE_TABLE);
    uart_puts("  L3 table at: 0x");
    uart_hex64((uint64_t)l3_table);
    uart_puts("\n");
    
    // Check if L3 entry is valid
    if (!(l3_table[l3_idx] & PTE_VALID)) {
        uart_puts("  L3 entry not valid!\n");
        return;
    }
    
    // Print L3 entry details
    uint64_t pte = l3_table[l3_idx];
    uart_puts("  L3 entry: 0x");
    uart_hex64(pte);
    uart_puts("\n");
    
    // Physical address
    uint64_t phys_addr = pte & ~0xFFFUL;
    uart_puts("  Physical address: 0x");
    uart_hex64(phys_addr);
    uart_puts("\n");
    
    // Check permissions
    uart_puts("  Permissions: ");
    if (pte & PTE_UXN) uart_puts("UXN ");
    if (pte & PTE_PXN) uart_puts("PXN ");
    if ((pte & PTE_AP_MASK) == PTE_AP_RW) uart_puts("RW ");
    if ((pte & PTE_AP_MASK) == PTE_AP_RO) uart_puts("RO ");
    uart_puts("\n");
    
    // Check if executable
    uart_puts("  Executable: ");
    if ((pte & (PTE_UXN | PTE_PXN)) == 0) {
        uart_puts("YES\n");
    } else {
        uart_puts("NO\n");
    }
}

// Define PTE_NORMAL and PTE_DEVICE for easier mapping
// #define PTE_NORMAL (PTE_VALID | PTE_AF | PTE_SH_INNER | (ATTR_IDX_NORMAL << 2))
// #define PTE_DEVICE (PTE_VALID | PTE_AF | PTE_SH_INNER | (ATTR_IDX_DEVICE_nGnRnE << 2))

uint64_t* create_page_table(void) {
    void* table = alloc_page();
    if (!table) {
        uart_puts("[VMM] Failed to allocate page table!\n");
        return NULL;
    }

    memset(table, 0, PAGE_SIZE);  // clear entries
    return (uint64_t*)table;
}

// Implementation of map_page function
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags) {
    if (l3_table == NULL) {
        uart_puts_early("[VMM] Error: L3 table is NULL in map_page\n");
        return;
    }
    
    // Check if we're trying to map in the UART region - avoid unnecessary double mappings
    if ((pa >= UART_PHYS && pa < (UART_PHYS + 0x1000)) ||
        (va >= UART_PHYS && va < (UART_PHYS + 0x1000))) {
        // Skip UART MMIO region to avoid collisions
        uart_puts_early("[VMM] Skipping UART region mapping at PA=0x");
        uart_hex64_early(pa);
        uart_puts_early(" VA=0x");
        uart_hex64_early(va);
        uart_puts_early("\n");
        return;
    }
    
    uint64_t l3_index = (va >> PAGE_SHIFT) & (ENTRIES_PER_TABLE - 1);
    uint64_t l3_entry = pa;
    
    // Add flags
    l3_entry |= PTE_PAGE;  // Mark as a page entry
    l3_entry |= flags;    // Add provided flags
    
    // Store the entry
    l3_table[l3_index] = l3_entry;
    
    // Debug output
    if (debug_vmm) {
        uart_puts_early("[VMM] Mapped VA 0x");
        uart_hex64_early(va);
        uart_puts_early(" to PA 0x");
        uart_hex64_early(pa);
        uart_puts_early(" with flags 0x");
        uart_hex64_early(flags);
        uart_puts_early(" at L3 index ");
        uart_hex64_early(l3_index);
        uart_puts_early("\n");
    }
}

// Function declarations
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
void init_vmm_impl(void);
void init_vmm_wrapper(void);
int verify_executable_address(uint64_t *table_ptr, uint64_t vaddr, const char* desc);
void map_code_section(void);

// Add map_range implementation before init_vmm_impl()
// Maps a range of virtual addresses to physical addresses
void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
               uint64_t phys_start, uint64_t flags) {
    uart_puts_early("[VMM] Mapping VA 0x");
    uart_hex64_early(virt_start);
    uart_puts_early(" - 0x");
    uart_hex64_early(virt_end);
    uart_puts_early(" to PA 0x");
    uart_hex64_early(phys_start);
    uart_puts_early(" with flags 0x");
    uart_hex64_early(flags);
    uart_puts_early("\n");
    
    // Calculate the number of pages
    uint64_t size = virt_end - virt_start;
    uint64_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Map each page
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t virt_addr = virt_start + (i * PAGE_SIZE);
        uint64_t phys_addr = phys_start + (i * PAGE_SIZE);
        
        // Create/get L3 table for the virtual address
        uint64_t* l3_table = get_l3_table_for_addr(l0_table, virt_addr);
        if (!l3_table) {
            uart_puts_early("[VMM] ERROR: Could not get L3 table for address 0x");
            uart_hex64_early(virt_addr);
            uart_puts_early("\n");
            continue;
        }
        
        // Calculate the L3 index
        uint64_t l3_idx = (virt_addr >> 12) & 0x1FF;
        
        // Create page table entry
        uint64_t pte = (phys_addr & ~0xFFF) | flags;
        
        // Cache maintenance before updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Write the mapping
        l3_table[l3_idx] = pte;
        
        // Cache maintenance after updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Perform explicit TLB invalidation for this address
        asm volatile("tlbi vaae1is, %0" :: "r"(virt_addr >> 12) : "memory");
        asm volatile("dsb ish" ::: "memory");
    }
    
    // Final TLB invalidation after all updates
    asm volatile("tlbi vmalle1is" ::: "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    
    // Register the mapping for diagnostic purposes
    register_mapping(virt_start, virt_end, phys_start, flags, "Range mapping");
}

// Kernel stack configuration
#define KERNEL_STACK_VA 0x400FF000  // Fixed virtual address for kernel stack
#define KERNEL_STACK_PATTERN 0xDEADBEEF

// External kernel function symbols
extern void task_a(void);
extern void known_alive_function(void);
extern char vector_table[];

// Note: Memory attribute and page permission macros are defined at the top of this file

// Test pattern to verify executable memory works and can be reached by ERET
void __attribute__((used, externally_visible, section(".text"))) eret_test_pattern(void) {
    // Direct UART access for maximum reliability
    volatile uint32_t *uart = (volatile uint32_t*)0x09000000;
    
    // Clear pattern - indicates successful execution
    for (int i = 0; i < 80; i++) {
        *uart = '=';
    }
    *uart = '\r';
    *uart = '\n';
    
    // Print header
    *uart = 'S';
    *uart = 'U';
    *uart = 'C';
    *uart = 'C';
    *uart = 'E';
    *uart = 'S';
    *uart = 'S';
    *uart = '!';
    *uart = ' ';
    *uart = 'E';
    *uart = 'R';
    *uart = 'E';
    *uart = 'T';
    *uart = ' ';
    *uart = 'W';
    *uart = 'O';
    *uart = 'R';
    *uart = 'K';
    *uart = 'S';
    *uart = '!';
    *uart = '\r';
    *uart = '\n';
    
    // Endless loop with visible heartbeat
    int counter = 0;
    while (1) {
        // Heart beat char
        *uart = '<';
        *uart = '3';
        *uart = ' ';
        
        // Counter display
        if (counter & 1) *uart = '1'; else *uart = '0';
        if (counter & 2) *uart = '1'; else *uart = '0';
        if (counter & 4) *uart = '1'; else *uart = '0';
        if (counter & 8) *uart = '1'; else *uart = '0';
        
        *uart = '\r';
        *uart = '\n';
        
        // Simple delay
        for (volatile int i = 0; i < 100000; i++) {
            // Empty busy-wait
            if (i == 50000) {
                *uart = '.'; // Mid-point marker
            }
        }
        
        counter++;
    }
}

// Debug function to print text section info
void print_text_section_info(void) {
    extern void* __text_start;
    extern void* __text_end;
    
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'T'; *uart = 'X'; *uart = 'T'; *uart = ':'; *uart = ' ';
    
    // Print _text_start address (high byte)
    uint64_t text_start = (uint64_t)&__text_start;
    uint8_t high_byte = (text_start >> 24) & 0xFF;
    *uart = ((high_byte >> 4) & 0xF) < 10 ? 
            '0' + ((high_byte >> 4) & 0xF) : 
            'A' + ((high_byte >> 4) & 0xF) - 10;
    *uart = (high_byte & 0xF) < 10 ? 
            '0' + (high_byte & 0xF) : 
            'A' + (high_byte & 0xF) - 10;
    
    *uart = '-';
    
    // Print _text_end address (high byte)
    uint64_t text_end = (uint64_t)&__text_end;
    high_byte = (text_end >> 24) & 0xFF;
    *uart = ((high_byte >> 4) & 0xF) < 10 ? 
            '0' + ((high_byte >> 4) & 0xF) : 
            'A' + ((high_byte >> 4) & 0xF) - 10;
    *uart = (high_byte & 0xF) < 10 ? 
            '0' + (high_byte & 0xF) : 
            'A' + (high_byte & 0xF) - 10;
    
    *uart = '\r';
    *uart = '\n';
}

// Function to debug memory permissions across different regions
void debug_memory_permissions(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = ' ';
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
}

// Function to verify code sections are executable
void verify_code_is_executable(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'F'; *uart = 'I'; *uart = 'X'; *uart = 'X'; *uart = ':'; // Fix execute permissions
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        *uart = 'L'; *uart = '0'; *uart = '!'; // L0 error
        return;
    }
    
    // Critical test functions we need to make executable
    extern void test_uart_direct(void);
    extern void test_scheduler(void);
    extern void dummy_asm(void);
    extern void known_branch_test(void);
    extern void full_restore_context(void*);
    
    // Array of critical function addresses to make executable
    uint64_t critical_addrs[] = {
        (uint64_t)&test_uart_direct,
        (uint64_t)&test_scheduler,
        (uint64_t)&dummy_asm,
        (uint64_t)&known_branch_test,
        (uint64_t)&full_restore_context,
        0 // End marker
    };
    
    // Fix execute permissions for all critical functions
    for (int i = 0; critical_addrs[i] != 0; i++) {
        uint64_t addr = critical_addrs[i];
        
        // Get the L3 table for this address
        uint64_t* l3_table = get_l3_table_for_addr(l0_table, addr);
        if (!l3_table) {
            *uart = 'L'; *uart = '3'; *uart = '!'; // L3 error
            continue;
        }
        
        // Calculate the L3 index
        uint64_t l3_idx = (addr >> 12) & 0x1FF;
        
        // Get current PTE
        uint64_t pte = l3_table[l3_idx];
        
        // Clear UXN and PXN bits to make executable
        uint64_t new_pte = pte & ~(PTE_UXN | PTE_PXN);
        
        // Update the PTE
        l3_table[l3_idx] = new_pte;
        
        // Output confirmation for this address
        *uart = 'X'; 
        *uart = '0' + i; // Index
    }
    
    // Flush TLB to ensure changes take effect
    __asm__ volatile("dsb ishst");
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
    
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
}

/**
 * Verify an address is properly mapped as executable through all page table levels
 * Returns 1 if address is executable, 0 otherwise
 */
int verify_executable_address(uint64_t *table_ptr, uint64_t vaddr, const char* desc) {
    uart_puts("\n=== VERIFYING EXECUTABLE MAPPING FOR ");
    uart_puts(desc);
    uart_puts(" (");
    uart_hex64(vaddr);
    uart_puts(") ===\n");
    
    // Extract indexes
    uint64_t l0_idx = (vaddr >> 39) & 0x1FF;
    uint64_t l1_idx = (vaddr >> 30) & 0x1FF;
    uint64_t l2_idx = (vaddr >> 21) & 0x1FF;
    uint64_t l3_idx = (vaddr >> 12) & 0x1FF;
    
    uart_puts("- L0 IDX: "); uart_hex64(l0_idx); 
    uart_puts(" L1 IDX: "); uart_hex64(l1_idx);
    uart_puts(" L2 IDX: "); uart_hex64(l2_idx);
    uart_puts(" L3 IDX: "); uart_hex64(l3_idx);
    uart_puts("\n");
    
    // Check L0 entry - FIXED: renamed to avoid shadowing
    uint64_t *l0_pt_verify = table_ptr;
    uint64_t l0_entry = l0_pt_verify[l0_idx];
    uart_puts("- L0 Entry: "); uart_hex64(l0_entry); uart_puts("\n");
    
    if (!(l0_entry & PTE_VALID)) {
        uart_puts("  ERROR: L0 entry not valid!\n");
        return 0;
    }
    
    // Check L1 entry
    uint64_t *l1_table = (uint64_t*)((l0_entry & PTE_TABLE_ADDR) & ~0xFFF);
    uart_puts("- L1 Table: "); uart_hex64((uint64_t)l1_table); uart_puts("\n");
    uint64_t l1_entry = l1_table[l1_idx];
    uart_puts("- L1 Entry: "); uart_hex64(l1_entry); uart_puts("\n");
    
    if (!(l1_entry & PTE_VALID)) {
        uart_puts("  ERROR: L1 entry not valid!\n");
        return 0;
    }
    
    // Check L2 entry
    uint64_t *l2_table = (uint64_t*)((l1_entry & PTE_TABLE_ADDR) & ~0xFFF);
    uart_puts("- L2 Table: "); uart_hex64((uint64_t)l2_table); uart_puts("\n");
    uint64_t l2_entry = l2_table[l2_idx];
    uart_puts("- L2 Entry: "); uart_hex64(l2_entry); uart_puts("\n");
    
    if (!(l2_entry & PTE_VALID)) {
        uart_puts("  ERROR: L2 entry not valid!\n");
        return 0;
    }
    
    // Check L3 entry
    uint64_t *l3_table = (uint64_t*)((l2_entry & PTE_TABLE_ADDR) & ~0xFFF);
    uart_puts("- L3 Table: "); uart_hex64((uint64_t)l3_table); uart_puts("\n");
    
    if (l3_table == NULL) {
        uart_puts("  ERROR: L3 table is NULL!\n");
        return 0;
    }
    
    uint64_t l3_entry = l3_table[l3_idx];
    uart_puts("- L3 Entry: "); uart_hex64(l3_entry); uart_puts("\n");
    
    if (!(l3_entry & PTE_VALID)) {
        uart_puts("  ERROR: L3 entry not valid!\n");
        return 0;
    }
    
    // Check for executable permission
    if (l3_entry & PTE_PXN) {
        uart_puts("  ERROR: Address is NOT executable (PXN bit set)!\n");
        return 0;
    }
    
    // Check access flags
    if (!(l3_entry & PTE_AF)) {
        uart_puts("  ERROR: Address does not have access flag set!\n");
        return 0;
    }
    
    uart_puts("  SUCCESS: Address is properly mapped as executable!\n");
    return 1;
}

// Implementation of get_l3_table_for_addr with auto-creation of missing levels
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr) {
    if (!l0_table) {
        uart_puts("[VMM] ERROR: L0 table is NULL in get_l3_table_for_addr\n");
        return NULL;
    }
    
    // Calculate indices for each level
    uint64_t l0_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t l1_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_idx = (virt_addr >> 21) & 0x1FF;
    
    // Debug output
    if (debug_vmm) {
        uart_puts("[VMM] Getting L3 table for VA 0x");
        uart_hex64(virt_addr);
        uart_puts(", L0[");
        uart_hex64(l0_idx);
        uart_puts("], L1[");
        uart_hex64(l1_idx);
        uart_puts("], L2[");
        uart_hex64(l2_idx);
        uart_puts("]\n");
    }
    
    // Step 1: L0 → L1
    if (!(l0_table[l0_idx] & PTE_VALID)) {
        uart_puts("[VMM] No L1 table for VA 0x");
        uart_hex64(virt_addr);
        uart_puts(", creating new L1 table\n");
        
        // Allocate a new L1 table
        uint64_t* new_l1 = alloc_page();
        if (!new_l1) {
            uart_puts("[VMM] ERROR: Failed to allocate L1 table\n");
            return NULL;
        }
        
        // Clear the new table
        memset(new_l1, 0, PAGE_SIZE);
        
        // Set the L0 entry to point to the new L1 table
        l0_table[l0_idx] = (uint64_t)new_l1 | PTE_VALID | PTE_TABLE;
        
        // Cache maintenance
        asm volatile (
            "dc cvac, %0\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(&l0_table[l0_idx]) : "memory"
        );
    }
    
    // Get L1 table
    uint64_t* l1_table = (uint64_t*)(l0_table[l0_idx] & ~0xFFF);
    
    // Step 2: L1 → L2
    if (!(l1_table[l1_idx] & PTE_VALID)) {
        uart_puts("[VMM] No L2 table for VA 0x");
        uart_hex64(virt_addr);
        uart_puts(", creating new L2 table\n");
        
        // Allocate a new L2 table
        uint64_t* new_l2 = alloc_page();
        if (!new_l2) {
            uart_puts("[VMM] ERROR: Failed to allocate L2 table\n");
            return NULL;
        }
        
        // Clear the new table
        memset(new_l2, 0, PAGE_SIZE);
        
        // Set the L1 entry to point to the new L2 table
        l1_table[l1_idx] = (uint64_t)new_l2 | PTE_VALID | PTE_TABLE;
        
        // Cache maintenance
        asm volatile (
            "dc cvac, %0\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(&l1_table[l1_idx]) : "memory"
        );
    }
    
    // Get L2 table
    uint64_t* l2_table = (uint64_t*)(l1_table[l1_idx] & ~0xFFF);
    
    // Step 3: L2 → L3
    if (!(l2_table[l2_idx] & PTE_VALID)) {
        uart_puts("[VMM] No L3 table for VA 0x");
        uart_hex64(virt_addr);
        uart_puts(", creating new L3 table\n");
        
        // Allocate a new L3 table
        uint64_t* new_l3 = alloc_page();
        if (!new_l3) {
            uart_puts("[VMM] ERROR: Failed to allocate L3 table\n");
            return NULL;
        }
        
        // Clear the new table
        memset(new_l3, 0, PAGE_SIZE);
        
        // Set the L2 entry to point to the new L3 table
        l2_table[l2_idx] = (uint64_t)new_l3 | PTE_VALID | PTE_TABLE;
        
        // Cache maintenance
        asm volatile (
            "dc cvac, %0\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(&l2_table[l2_idx]) : "memory"
        );
    }
    
    // Return the L3 table
    return (uint64_t*)(l2_table[l2_idx] & ~0xFFF);
}

// Simple overload of map_page that gets the right L3 table automatically
void map_page_direct(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    if (l0_table == NULL) {
        uart_puts("[VMM] ERROR: Cannot map page - l0_table not initialized\n");
        return;
    }
    
    // Get the L3 table for this address
    uint64_t* l3_pt_local = get_l3_table_for_addr(l0_table, va);
    if (l3_pt_local == NULL) {
        uart_puts("[VMM] ERROR: Failed to get L3 table for address 0x");
        uart_hex64(va);
        uart_puts("\n");
        return;
    }
    
    // Map each page in the range
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t page_va = va + offset;
        uint64_t page_pa = pa + offset;
        
        // Use the full version of map_page that takes an L3 table
        map_page(l3_pt_local, page_va, page_pa, flags);
    }
}

// Function name change from map_page to map_page_region
// Change the implementation at line 1548
void map_page_region(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    if (l0_table == NULL) {
        uart_puts("[VMM] ERROR: Cannot map page - l0_table not initialized\n");
        return;
    }
    
    // Get the L3 table for this address
    uint64_t* l3_pt_local = get_l3_table_for_addr(l0_table, va);
    if (l3_pt_local == NULL) {
        uart_puts("[VMM] ERROR: Failed to get L3 table for address 0x");
        uart_hex64(va);
        uart_puts("\n");
        return;
    }
    
    // Map each page in the range
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t page_va = va + offset;
        uint64_t page_pa = pa + offset;
        
        // Use the full version of map_page that takes an L3 table
        map_page(l3_pt_local, page_va, page_pa, flags);
    }
}

// Update all calls to this function to use the new name
// For example, change:
//   map_page(0x3F200000, 0x3F200000, 0x1000, PTE_DEVICE | PTE_RW);
// to:
//   map_page_region(0x3F200000, 0x3F200000, 0x1000, PTE_DEVICE | PTE_RW);

void init_mmu_after_el1(void) {
    // ... existing code ...
    
    // Allocate page for L0 table (512 entries = 4KB)
    // FIXED: Use direct assignment to global instead of shadowing
    l0_table = alloc_page();
    if (l0_table == NULL) {
        uart_puts("[INIT] FATAL: Failed to allocate L0 page table\n");
        return;
    }
    uart_puts("[INIT] L0 page table allocated at 0x");
    uart_hex64((uint64_t)l0_table);
    uart_puts("\n");
    
    // Clear the L0 table
    memset(l0_table, 0, PAGE_SIZE);
    
    // Identity map the first 1GB of memory
    // First, create an L1 table
    uint64_t* l1_pt_local = alloc_page();
    if (l1_pt_local == NULL) {
        uart_puts("[INIT] FATAL: Failed to allocate L1 page table\n");
        return;
    }
    uart_puts("[INIT] L1 page table allocated at 0x");
    uart_hex64((uint64_t)l1_pt_local);
    uart_puts("\n");
    
    // Clear the L1 table
    memset(l1_pt_local, 0, PAGE_SIZE);
    
    // Set up the L0 entry to point to the L1 table
    uint64_t l0_index = 0; // First entry in L0 table
    uint64_t l0_entry = (uint64_t)l1_pt_local;
    l0_entry |= PTE_TABLE | PTE_VALID;  // Mark as table descriptor AND valid
    
    // FIXED: Use write_phys64 instead of direct pointer assignment
    write_phys64((uint64_t)l0_table + l0_index * sizeof(uint64_t), l0_entry);
    
    uart_puts("[INIT] L0 entry 0 set to 0x");
    uart_hex64(l0_entry);
    uart_puts("\n");
    
    // ... existing code ...
}

// Function to map the executable code region with proper permissions
void map_code_section(void) {
    uart_puts("[VMM] Explicitly mapping code section (0x40080000-0x40090000)\n");
    
    // Use page-level mappings (4KB), loop over 0x40080000 → 0x40090000
    for (uint64_t addr = 0x40080000; addr < 0x40090000; addr += PAGE_SIZE) {
        // Get L3 table for this address - will auto-create missing tables
        uint64_t* l3_table = get_l3_table_for_addr(l0_table, addr);
        if (!l3_table) {
            uart_puts("[VMM] ERROR: Failed to get L3 table for code section at 0x");
            uart_hex64(addr);
            uart_puts("\n");
            continue;
        }
        
        // Map this page with executable permissions
        uint64_t l3_idx = (addr >> 12) & 0x1FF;
        uint64_t pte = addr;  // Physical address = Virtual address (identity mapping)
        
        // Set flags for executable code
        pte |= PTE_VALID | PTE_AF | PTE_SH_INNER | (ATTR_IDX_NORMAL << 2) | PTE_PAGE;
        
        // Clear UXN and PXN bits to make it executable
        // This is the key flag change compared to data pages
        pte &= ~(PTE_UXN | PTE_PXN);
        
        // Set RW permissions
        pte |= PTE_AP_RW;
        
        // Set the entry in the L3 table
        l3_table[l3_idx] = pte;
        
        // Cache maintenance
        asm volatile (
            "dc cvac, %0\n"
            "dsb ish\n"
            "isb\n"
            :: "r"(&l3_table[l3_idx]) : "memory"
        );
        
        if (debug_vmm) {
            uart_puts("[VMM] Mapped executable page at VA 0x");
            uart_hex64(addr);
            uart_puts(" with PTE 0x");
            uart_hex64(pte);
            uart_puts("\n");
        }
    }
    
    uart_puts("[VMM] Code section mapping complete\n");
    
    // Flush TLB to ensure changes take effect
    asm volatile (
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory"
    );
}

// Map vector_table with proper executable permissions
void map_vector_table(void) {
    uart_puts_early("[VMM] Mapping vector table\n");
    
    // Use the global kernel L0 table
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: L0 table is NULL in map_vector_table\n");
        return;
    }
    
    // Get the vector table address
    uint64_t vbar_addr = read_vbar_el1();
    uint64_t vbar_virt = vbar_addr;
    
    uart_puts_early("[VMM] Vector table at physical address: 0x");
    uart_hex64_early(vbar_addr);
    uart_puts_early("\n");
    
    // Calculate page-aligned addresses
    uint64_t vbar_page_start = vbar_addr & ~0xFFF;
    uint64_t vbar_page_end = (vbar_addr + 0x1000) & ~0xFFF;
    
    // Map the vector table page as executable
    map_range(l0_table, 
              vbar_page_start, 
              vbar_page_end + 0x1000, // Map an extra page for safety
              vbar_page_start, 
              PTE_KERN_TEXT); // Use executable mapping
    
    // Register the mapping
    register_mapping(vbar_page_start, vbar_page_end + 0x1000, 
                    vbar_page_start, PTE_KERN_TEXT, "Vector Table");
    
    // Get the L3 table for this address
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, vbar_virt);
    if (l3_table) {
        // Make sure the vector table is executable
        ensure_vector_table_executable_l3(l3_table);
    } else {
        uart_puts_early("[VMM] ERROR: Could not get L3 table for vector table\n");
    }
    
    uart_puts_early("[VMM] Vector table mapped\n");
}

// Make sure the vector table is executable - auto-finds the L3 table

// Map a page in the kernel address space
// This is a convenience wrapper that gets the kernel page table,
// finds the appropriate L3 table, and maps the page
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t flags) {
    debug_print("[VMM] Mapping kernel page VA 0x");
    debug_hex64("", va);
    debug_print(" to PA 0x");
    debug_hex64("", pa);
    debug_print("\n");
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        debug_print("[VMM] ERROR: Could not get kernel page table!\n");
        return;
    }
    
    // Get the L3 table for the address
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, va);
    if (!l3_table) {
        debug_print("[VMM] ERROR: Could not get L3 table for address!\n");
        return;
    }
    
    // Map the page
    map_page(l3_table, va, pa, flags);
    
    // Flush TLB to ensure changes take effect
    __asm__ volatile("dsb ishst");
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
    
    debug_print("[VMM] Kernel page mapped successfully\n");
}

// Map user task section with proper permissions for EL0 access
void map_user_task_section(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = '['; *uart = 'U'; *uart = 'S'; *uart = 'R'; *uart = ']'; // Direct UART debug
    
    extern void user_test_svc(void);  // Changed from user_task to user_test_svc
    uint64_t user_task_addr = (uint64_t)&user_test_svc;  // Changed from user_task to user_test_svc
    
    // Print address directly to UART
    *uart = 'A'; *uart = 'D'; *uart = 'D'; *uart = 'R'; *uart = ':';
    uint8_t* addr_bytes = (uint8_t*)&user_test_svc;
    for (int i = 7; i >= 0; i--) {
        uint8_t byte = addr_bytes[i];
        uint8_t high = (byte >> 4) & 0xF;
        uint8_t low = byte & 0xF;
        
        *uart = high < 10 ? '0' + high : 'A' + (high - 10);
        *uart = low < 10 ? '0' + low : 'A' + (low - 10);
    }
    *uart = '\r'; *uart = '\n';
    
    debug_print("[VMM] Mapping user task section with EL0 permissions\n");
    debug_print("[VMM] User task address: 0x");
    debug_hex64("", user_task_addr);
    debug_print("\n");
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        debug_print("[VMM] ERROR: Could not get kernel page table!\n");
        return;
    }
    
    // Calculate page-aligned address
    uint64_t page_addr = user_task_addr & ~0xFFF; // Page-aligned address
    
    // Map 3 pages (12KB) for user task code - to be safe
    for (int i = 0; i < 3; i++) {
        uint64_t vaddr = page_addr + (i * 0x1000);
        uint64_t paddr = vaddr;  // Identity mapping
        
        // Use special permissions for user task: executable for EL0
        // Clear UXN (Unprivileged Execute Never) bit to make it executable from EL0
        // Set AP[1] to allow EL0 access (PTE_AP_USER)
        map_kernel_page(vaddr, paddr, 
                 PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_AP_USER | PTE_KERNEL_EXEC);
        
        debug_print("[VMM] Mapped user task page at VA: 0x");
        debug_hex64("", vaddr);
        debug_print(" to PA: 0x");
        debug_hex64("", paddr);
        debug_print(" with EL0 executable permissions\n");
    }
    
    debug_print("[VMM] User task section mapped with EL0 permissions\n");
}

// Global variables for VMM - defined at the top of the file

// Original MMU enable function preserved for backward compatibility
void enable_mmu(uint64_t* page_table_base) {
    // Note: We should already have mapped the vector table and saved its address
    // in saved_vector_table_addr before enabling MMU
    
    uart_puts_early("[VMM] Enabling MMU with vector table mapped at 0x");
    uart_hex64_early(saved_vector_table_addr);
    uart_puts_early("\n");
    
    // CRITICAL: Verify VBAR_EL1 is set before enabling MMU
    uint64_t current_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    uart_puts_early("[VMM] PRE-MMU VBAR_EL1: 0x");
    uart_hex64_early(current_vbar);
    uart_puts_early("\n");
    
    // If VBAR_EL1 is not set or doesn't match our mapped address, set it now
    if (current_vbar == 0 || (saved_vector_table_addr != 0 && current_vbar != saved_vector_table_addr)) {
        uint64_t target_vbar = (saved_vector_table_addr != 0) ? saved_vector_table_addr : (uint64_t)vector_table;
        uart_puts_early("[VMM] Setting VBAR_EL1 to 0x");
        uart_hex64_early(target_vbar);
        uart_puts_early(" before enabling MMU\n");
        
        // Set VBAR_EL1 
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(target_vbar)
        );
        
        // Verify
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        uart_puts_early("[VMM] VBAR_EL1 verification: 0x");
        uart_hex64_early(current_vbar);
        uart_puts_early("\n");
    }
    
    // CRITICAL: Map UART to virtual address so we can use it after MMU is enabled
    uart_puts_early("[VMM] Mapping UART virtual address before enabling MMU\n");
    map_uart();
    
    // PRE-MMU SYNCHRONIZATION POINT
    // Add memory barriers to ensure all writes are committed before enabling MMU
    uart_puts_early("[VMM] Memory barrier before enabling MMU\n");
    asm volatile("dsb ish" ::: "memory");  // Commit all memory writes using inner-shareable domain
    
    extern void mmu_continuation_point(void);
    
    // Copy crucial values to emergency debug registers to help diagnose crashes
    asm volatile (
        "mov x19, %0\n"              // Save page table base to x19
        "mov x20, %1\n"              // Save continuation point address to x20
        "mrs x21, vbar_el1\n"        // Save VBAR_EL1 to x21
        :: "r"(page_table_base), "r"(mmu_continuation_point)
        : "x19", "x20", "x21"
    );
    
    // Full MMU initialization sequence for ARMv8-A architecture with immediate VBAR_EL1 update
    // and explicit branch to continuation point
    asm volatile (
        // 1. Store continuation point address in x9
        "adr x9, %1\n"              // Load physical address of mmu_continuation_point

        // 2. Ensure all previous memory accesses complete
        "dsb ish\n"                 // Data Synchronization Barrier (Inner Shareable domain)

        // 3. Configure memory attributes (MAIR_EL1)
        // ------------------------------------------
        // Attr0 = 0b11111111 (Normal memory, Write-Back cacheable)
        // Attr1 = 0b00000100 (Device-nGnRE memory)
        "mov x0, #0xFF\n"            // Set MAIR[0] = 0xFF (lower 8 bits)
        "movk x0, #0x44, lsl #16\n"  // Set MAIR[2] = 0x44 (bits 16-23)
        "msr mair_el1, x0\n"         // Write to Memory Attribute Indirection Register

        // 4. Configure translation control (TCR_EL1)
        // ------------------------------------------
        // TG0 = 00 (4KB granule size)
        // T0SZ = 16 (64 - 16 = 48-bit virtual address space)
        // IPS = 001 (36-bit physical address space)
        "mov x0, #0x19\n"            // T0SZ[5:0] = 0x19 (25), but actually 16 due to bit math
        "movk x0, #0x1, lsl #32\n"   // IPS[2:0] = 0b001 (bits 32-34)
        "msr tcr_el1, x0\n"          // Write to Translation Control Register

        // 5. Set page table base address
        "msr ttbr0_el1, %0\n"        // TTBR0_EL1 = page_table_base (user space)
        "msr ttbr1_el1, xzr\n"       // TTBR1_EL1 = 0 (disable higher-half kernel mappings)
        
        // 6. Drain write buffer and invalidate TLBs before MMU enable
        "dsb ish\n"                  // Ensure all prior writes complete
        "tlbi vmalle1is\n"           // Invalidate all TLB entries at EL1 (inner-shareable)
        "ic iallu\n"                 // Invalidate instruction caches
        "dsb ish\n"                  // Wait for TLB/cache operations to complete
        "isb\n"                      // Instruction Synchronization Barrier

        // 7. Enable MMU
        "mrs x0, sctlr_el1\n"        // Read System Control Register
        "orr x0, x0, #1\n"           // Set M bit (bit 0) to enable MMU
        "msr sctlr_el1, x0\n"        // Write back modified SCTLR_EL1
        "isb\n"                      // Final barrier after MMU enable

        // 8. CRITICAL: Branch to identity-mapped continuation point
        "br x9\n"                    // Branch to mmu_continuation_point
        
        // We should never reach here, but in case we do:
        // Emergency failsafe code using direct UART access
        "mov x0, #'F'\n"             // 'F' for Fail
        "mov x1, #0x9000\n"
        "movk x1, #0x0, lsl #16\n"   // x1 = 0x09000000 (UART address)
        
        // Write 'FAIL' to UART
        "str w0, [x1]\n"             // F
        "mov x0, #'A'\n"
        "str w0, [x1]\n"             // A
        "mov x0, #'I'\n"
        "str w0, [x1]\n"             // I
        "mov x0, #'L'\n"
        "str w0, [x1]\n"             // L
        
        // Dump debug registers to UART to see what failed
        "mov x0, #'P'\n"             // P for Page table
        "str w0, [x1]\n"
        "mov x0, x19\n"              // Page table address 
        "bl 1f\n"                    // Call hex dump routine
        
        "mov x0, #'C'\n"             // C for Continuation point
        "str w0, [x1]\n"
        "mov x0, x20\n"              // Continuation point address
        "bl 1f\n"                    // Call hex dump routine
        
        "mov x0, #'V'\n"             // V for VBAR
        "str w0, [x1]\n"
        "mov x0, x21\n"              // VBAR_EL1 value
        "bl 1f\n"                    // Call hex dump routine
        
        "b .\n"                      // Hang forever
        
        // Simple hex dump routine to output register value to UART
        "1:\n"                       // Hex dump subroutine
        "mov x2, #16\n"              // 16 nibbles (64-bit)
        "2:\n"                       // Loop start
        "sub x2, x2, #1\n"           // Decrement nibble count
        "lsl x4, x2, #2\n"           // Calculate shift amount (nibble * 4)
        "lsr x3, x0, x4\n"           // Shift right by calculated amount
        "and x3, x3, #0xF\n"         // Mask to get nibble
        "cmp x3, #10\n"              // Check if 0-9 or A-F
        "b.ge 3f\n"                  // Branch if ≥ 10
        "add x3, x3, #'0'\n"         // Convert 0-9 to ASCII
        "b 4f\n"                     // Skip next instruction
        "3:\n"                       // Handle A-F
        "sub x3, x3, #10\n"          // Subtract 10
        "add x3, x3, #'A'\n"         // Convert to A-F
        "4:\n"                       // Continue
        "str w3, [x1]\n"             // Output character to UART
        "cbnz x2, 2b\n"              // Continue if not done
        "mov x0, #'\r'\n"            // Carriage return
        "str w0, [x1]\n"
        "mov x0, #'\n'\n"            // Line feed
        "str w0, [x1]\n"
        "ret\n"                      // Return
        
        : // No outputs
        : "r"(page_table_base),      // %0: page table root (L0) address
          "S"(mmu_continuation_point) // %1: mmu_continuation_point symbol
        : "x0", "x1", "x2", "x3", "x4", "x9" // Clobbered registers
    );
    
    // We should never reach here since we branch to mmu_continuation_point
    uart_puts_early("[VMM] ERROR: Returned from MMU enable sequence without branching!\n");
}

// Function to ensure VBAR_EL1 is correct after MMU is enabled
uint64_t get_pte(uint64_t virt_addr); // Forward declaration to fix compiler error

void ensure_vbar_after_mmu(void) {
    // Enhanced debug header
    uart_puts("\n[VMM] ====== VBAR_EL1 POST-MMU CHECK ======\n");
    
    // Get current VBAR_EL1 value
    uint64_t current_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    uart_puts("[VMM] VBAR_EL1 after MMU initialization: 0x");
    uart_hex64(current_vbar);
    uart_puts("\n");
    
    // We should have explicitly saved the vector table address when mapping it
    uart_puts("[VMM] Saved vector table address: 0x");
    uart_hex64(saved_vector_table_addr);
    uart_puts("\n");
    
    // If saved_vector_table_addr is 0, we haven't mapped it yet
    if (saved_vector_table_addr == 0) {
        uart_puts("[VMM] ERROR: No saved vector table address. Vector table not mapped?\n");
        return;
    }
    
    // Verify if the vector table address is mapped in the MMU
    uint64_t vt_pte = get_pte(saved_vector_table_addr);
    uart_puts("[VMM] Vector table PTE: 0x");
    uart_hex64(vt_pte);
    
    if (vt_pte & PTE_VALID) {
        uart_puts(" (VALID)\n");
        
        // Check executable permissions
        if (vt_pte & PTE_PXN) {
            uart_puts("[VMM] WARNING: Vector table is NOT marked executable! (PXN bit set)\n");
        } else {
            uart_puts("[VMM] Vector table is correctly marked executable (PXN bit clear)\n");
        }
    } else {
        uart_puts(" (INVALID - NOT MAPPED)\n");
        uart_puts("[VMM] CRITICAL: Vector table virtual address is not properly mapped!\n");
    }
    
    // Check if VBAR_EL1 is 0 or doesn't match our saved address
    if (current_vbar == 0 || current_vbar != saved_vector_table_addr) {
        uart_puts("[VMM] CRITICAL: VBAR_EL1 is incorrect! Setting to mapped address 0x");
        uart_hex64(saved_vector_table_addr);
        uart_puts("\n");
        
        // Set VBAR_EL1 to the explicitly mapped address
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(saved_vector_table_addr)
        );
        
        // Verify the update
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        uart_puts("[VMM] After update, VBAR_EL1 = 0x");
        uart_hex64(current_vbar);
        
        if (current_vbar == saved_vector_table_addr) {
            uart_puts(" (SUCCESS)\n");
        } else {
            uart_puts(" (FAILED - could not update!)\n");
            uart_puts("[VMM] ERROR: Failed to set VBAR_EL1 to the mapped address!\n");
        }
    } else {
        uart_puts("[VMM] VBAR_EL1 is correctly set to mapped vector table address\n");
    }
    
    uart_puts("[VMM] ====== END VBAR_EL1 POST-MMU CHECK ======\n");
}

// Get the L3 table for kernel mappings
uint64_t* get_kernel_l3_table(void) {
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        uart_puts("[VMM] ERROR: Could not get kernel page table for L3 table retrieval!\n");
        return NULL;
    }
    
    // Get the L3 table for kernel virtual address range
    // We'll use a standard kernel address like 0x1000000 to find the right L3 table
    uint64_t kernel_addr = 0x1000000;  // Address to look up
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, kernel_addr);
    
    if (!l3_table) {
        uart_puts("[VMM] ERROR: Could not get L3 table for kernel address!\n");
        return NULL;
    }
    
    return l3_table;
}

// Verify that a page has been correctly mapped in the page tables
void verify_page_mapping(uint64_t va) {
    uart_puts("[VMM] Verifying page mapping for VA 0x");
    uart_hex64(va);
    uart_puts("...\n");
    
    // Get PTE for the address
    uint64_t pte = get_pte(va);
    
    if (pte & PTE_VALID) {
        uart_puts("[VMM] PTE for VA is valid: 0x");
        uart_hex64(pte);
        uart_puts("\n");
        
        // Check if it's executable
        if ((pte & PTE_PXN) == 0) {
            uart_puts("[VMM] Page is executable (PXN is clear)\n");
        } else {
            uart_puts("[VMM] Page is NOT executable (PXN is set)\n");
        }
    } else {
        uart_puts("[VMM] ERROR: VA not mapped (PTE not valid)\n");
    }
}

// Function to ensure identity mapping for critical MMU transition code
void map_mmu_transition_code(void) {
    uart_puts_early("[VMM] Mapping MMU transition code\n");
    
    // Use the global kernel L0 table
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: L0 table is NULL in map_mmu_transition_code\n");
        return;
    }
    
    // Get the address of the continuation point function
    uint64_t continuation_addr = (uint64_t)&mmu_continuation_point;
    
    // Calculate page-aligned addresses
    uint64_t continuation_page_start = continuation_addr & ~0xFFF;
    uint64_t continuation_page_end = ((continuation_addr + 0x1000) & ~0xFFF);
    
    uart_puts_early("[VMM] MMU continuation point at address: 0x");
    uart_hex64_early(continuation_addr);
    uart_puts_early("\n");
    
    // Map the page containing the continuation point as executable
    map_range(l0_table, 
              continuation_page_start, 
              continuation_page_end, 
              continuation_page_start, 
              PTE_KERN_TEXT); // Use executable mapping
    
    // Register the mapping
    register_mapping(continuation_page_start, continuation_page_end, 
                    continuation_page_start, PTE_KERN_TEXT, "MMU Transition Code");
    
    // Verify the mapping
    uint64_t pte = get_pte(continuation_addr);
    
    uart_puts_early("[VMM] MMU transition code PTE: 0x");
    uart_hex64_early(pte);
    uart_puts_early("\n");
    
    if (!(pte & PTE_VALID)) {
        uart_puts_early("[VMM] ERROR: MMU transition code not properly mapped!\n");
    } else if (pte & PTE_PXN) {
        uart_puts_early("[VMM] WARNING: MMU transition code mapped as non-executable!\n");
        
        // Get the L3 table for this address
        uint64_t* l3_table = get_l3_table_for_addr(l0_table, continuation_addr);
        if (l3_table) {
            // Calculate the L3 index
            uint64_t l3_idx = (continuation_addr >> 12) & 0x1FF;
            
            // Clear the PXN bit to make it executable
            l3_table[l3_idx] &= ~PTE_PXN;
            
            // Cache maintenance
            asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
            asm volatile("dsb ish" ::: "memory");
            
            // Invalidate TLB for this entry
            asm volatile("tlbi vaae1is, %0" :: "r"(continuation_addr >> 12) : "memory");
            asm volatile("dsb ish" ::: "memory");
            asm volatile("isb" ::: "memory");
            
            uart_puts_early("[VMM] Fixed MMU transition code to be executable\n");
        }
    } else {
        uart_puts_early("[VMM] MMU transition code properly mapped as executable\n");
    }
}

// Simple wrapper function to call init_vmm_impl
void init_vmm_wrapper(void) {
    init_vmm_impl();
    
    // Create identity mapping for MMU transition code
    map_mmu_transition_code();
    
    // Map the vector table to 0x1000000 before enabling MMU
    map_vector_table();
    
    // Now enable the MMU
    enable_mmu(l0_table);
}

// Function to hold the current version of init_vmm as we transition
void init_vmm(void) {
    // Create initial debug output
    uart_puts("[VMM] Initializing virtual memory system\n");
    
    // Initialize page tables
    init_vmm_impl();
    
    // Map the vector table before enabling MMU
    map_vector_table();
    
    // Map UART to a virtual address for use after MMU is enabled
    map_uart();
    
    // Map transition code (MMU continuation point)
    map_mmu_transition_code();
    
    // Phase 9: Audit memory mappings for overlaps before enabling MMU
    audit_memory_mappings();
    
    // Verify memory permissions before enabling MMU
    verify_code_is_executable();
    
    // Enable MMU with our page table
    enable_mmu(l0_table);
    
    // We should never reach here since we branch to mmu_continuation_point in enable_mmu
    uart_puts_early("[VMM] ERROR: Returned from enable_mmu without branching!\n");
}

// Return the kernel's L0 (top-level) page table
uint64_t* get_kernel_page_table(void) {
    return l0_table;
}

// Continuation point for after MMU is enabled
void __attribute__((used, aligned(4096))) mmu_continuation_point(void) {
    // Critical: Use early UART functions here that directly access physical addresses
    // since the virtual mappings may not be working yet
    uart_puts_early("[MMU] Continuation point entered, MMU enabled\n");
    
    // CRITICAL: Explicit MMU transition point
    uart_set_base((void*)UART_VIRT);  // Cast to void* to fix type error
    
    // Diagnostic: verify UART base was properly updated
    extern volatile uint32_t* g_uart_base;
    uart_emergency_output('B');  // 'B' for Base address check
    uart_emergency_hex64((uint64_t)g_uart_base);  // Output the actual g_uart_base value
    uart_emergency_output('\r');
    uart_emergency_output('\n');
    
    // Complete synchronization barriers
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb" ::: "memory");

    // Step 1: Eliminate risky string literals - use character array instead
    char mmu_msg[64];  // Buffer for the message
    
    // Populate buffer character by character
    mmu_msg[0] = 'M'; mmu_msg[1] = 'M'; mmu_msg[2] = 'U'; 
    mmu_msg[3] = '-'; mmu_msg[4] = 'O'; mmu_msg[5] = 'K'; 
    mmu_msg[6] = '\r'; mmu_msg[7] = '\n'; mmu_msg[8] = '\0';
    
    // Step 2: Use the new helper function to flush the cache for the buffer
    flush_cache_lines(mmu_msg, sizeof(mmu_msg));
    
    // Step 4: Insert emergency debug check before print
    uart_emergency_output('1');
    
    // Step 5: Confirm pointer translation
    uart_emergency_hex64((uint64_t)&mmu_msg[0]);
    
    // Step 3: Use safe indexed function to output each character
    for (int i = 0; i < 64 && mmu_msg[i]; i++) {
        uart_putc_late(mmu_msg[i]);
    }
    
    // Step 4: Insert emergency debug check after print
    uart_emergency_output('2');
    
    // Step 8: Add hex dump of first few characters
    for (int i = 0; i < 4; i++) {
        uart_emergency_hex64(mmu_msg[i]);
    }
    
    // Test another safe message
    char test_msg[64];
    test_msg[0] = 'O'; test_msg[1] = 'K'; test_msg[2] = '\r'; 
    test_msg[3] = '\n'; test_msg[4] = '\0';
    
    // Flush cache for this buffer too using the helper
    flush_cache_lines(test_msg, sizeof(test_msg));
    
    // Debug marker
    uart_emergency_output('3');
    
    // Output message character by character
    for (int i = 0; i < 64 && test_msg[i]; i++) {
        uart_putc_late(test_msg[i]);
    }
    
    // Final success marker
    uart_emergency_output('F');
}

// ... existing code ...

void map_uart(void) {
    uart_puts_early("[VMM] Mapping UART MMIO region\n");
    
    // Make sure we have access to kernel L0 table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: Failed to get kernel page table for UART mapping\n");
        return;
    }
    
    // Create/get L3 table for the UART virtual address region
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, UART_VIRT);
    if (!l3_table) {
        uart_puts_early("[VMM] ERROR: Could not get L3 table for UART virtual address\n");
        return;
    }
    
    // Device memory attributes for UART MMIO - Using more correct PTE_DEVICE_nGnRE
    uint64_t uart_flags = PTE_DEVICE_nGnRE;
    
    // Debug output before mapping
    uart_puts_early("[VMM] UART mapping: PA 0x");
    uart_hex64_early(UART_PHYS);
    uart_puts_early(" -> VA 0x");
    uart_hex64_early(UART_VIRT);
    uart_puts_early(" with flags 0x");
    uart_hex64_early(uart_flags);
    uart_puts_early("\n");
    
    // Calculate the L3 index
    uint64_t l3_idx = (UART_VIRT >> 12) & 0x1FF;
    
    // Create page table entry
    uint64_t pte = UART_PHYS | uart_flags;
    
    // Perform proper cache maintenance on the page table entry before writing
    asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
    asm volatile("dsb ish" ::: "memory");
    
    // Write the mapping
    l3_table[l3_idx] = pte;
    
    // Cache maintenance after updating the PTE
    asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
    asm volatile("dsb ish" ::: "memory");
    
    // Perform explicit TLB invalidation for this specific address
    asm volatile("tlbi vaae1is, %0" :: "r"(UART_VIRT >> 12) : "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    
    // Verify the mapping was set correctly
    uint64_t read_pte = l3_table[l3_idx];
    uart_puts_early("[VMM] Verified UART PTE: 0x");
    uart_hex64_early(read_pte);
    uart_puts_early("\n");
    
    // Save the phys/virt addresses for diagnostic use
    register_mapping(UART_VIRT, UART_VIRT + 0x1000, UART_PHYS, uart_flags, "UART MMIO");
}

// Function to verify UART mapping after MMU is enabled
void verify_uart_mapping(void) {
    uart_puts_safe_indexed("[VMM] Verifying UART virtual mapping post-MMU\n");
    
    // Get UART PTE from page tables
    uint64_t pte = get_pte(UART_VIRT);
    
    // Output PTE value for verification
    uart_puts_safe_indexed("[VMM] UART PTE post-MMU: 0x");
    uart_emergency_hex64(pte);
    uart_puts_safe_indexed("\n");
    
    // Check if the PTE is valid
    if (!(pte & PTE_VALID)) {
        uart_puts_safe_indexed("[VMM] ERROR: UART mapping is not valid!\n");
        return;
    }
    
    // Check memory attributes
    uint64_t attr_idx = (pte >> 2) & 0x7;
    uart_puts_safe_indexed("[VMM] UART memory attribute index: ");
    uart_emergency_hex64(attr_idx);
    uart_puts_safe_indexed("\n");
    
    // Verify UART functionality by reading UART registers
    volatile uint32_t* uart_fr = (volatile uint32_t*)(UART_VIRT + 0x18);
    uint32_t fr_val = *uart_fr;
    
    uart_puts_safe_indexed("[VMM] UART FR register value: 0x");
    uart_emergency_hex64(fr_val);
    uart_puts_safe_indexed("\n");
    
    uart_puts_safe_indexed("[VMM] UART mapping verification complete\n");
}

// ... existing code ...

// Function to read a page table entry for a virtual address
uint64_t read_pte_entry(uint64_t va) {
    // Use the existing get_pte function for implementation
    return get_pte(va);
}

// ... existing code ...

// Function to get a page table entry for a virtual address
uint64_t get_pte(uint64_t virt_addr) {
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        uart_puts("[VMM] ERROR: No kernel page table available for get_pte!\n");
        return 0;
    }
    
    // Calculate indices for each level
    uint64_t l0_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t l1_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t l3_idx = (virt_addr >> 12) & 0x1FF;
    
    // Check L0 entry
    if (!(l0_table[l0_idx] & PTE_VALID)) {
        return 0; // L0 entry not valid
    }
    
    // Get L1 table
    uint64_t* l1_table = (uint64_t*)((l0_table[l0_idx] & PTE_ADDR_MASK) & ~0xFFF);
    
    // Check L1 entry
    if (!(l1_table[l1_idx] & PTE_VALID)) {
        return 0; // L1 entry not valid
    }
    
    // Get L2 table
    uint64_t* l2_table = (uint64_t*)((l1_table[l1_idx] & PTE_ADDR_MASK) & ~0xFFF);
    
    // Check L2 entry
    if (!(l2_table[l2_idx] & PTE_VALID)) {
        return 0; // L2 entry not valid
    }
    
    // Get L3 table
    uint64_t* l3_table = (uint64_t*)((l2_table[l2_idx] & PTE_ADDR_MASK) & ~0xFFF);
    
    // Check L3 entry
    if (!l3_table) {
        return 0; // L3 table is NULL
    }
    
    // Return the L3 page table entry
    return l3_table[l3_idx];
}

// ... existing code ...

// Structure to store memory mapping information
typedef struct {
    uint64_t virt_start;
    uint64_t virt_end;
    uint64_t phys_start;
    uint64_t flags;
    const char* name;
} MemoryMapping;

#define MAX_MAPPINGS 32
static MemoryMapping mappings[MAX_MAPPINGS];
static int num_mappings = 0;

// Function to register a memory mapping for diagnostic purposes
void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, uint64_t flags, const char* name) {
    if (num_mappings >= MAX_MAPPINGS) {
        uart_puts_early("[VMM] WARNING: Too many mappings registered, ignoring mapping for ");
        uart_puts_early(name);
        uart_puts_early("\n");
        return;
    }
    
    mappings[num_mappings].virt_start = virt_start;
    mappings[num_mappings].virt_end = virt_end;
    mappings[num_mappings].phys_start = phys_start;
    mappings[num_mappings].flags = flags;
    mappings[num_mappings].name = name;
    
    num_mappings++;
    
    if (debug_vmm) {
        uart_puts_early("[VMM] Registered mapping: ");
        uart_puts_early(name);
        uart_puts_early(" VA: 0x");
        uart_hex64_early(virt_start);
        uart_puts_early(" - 0x");
        uart_hex64_early(virt_end);
        uart_puts_early(" PA: 0x");
        uart_hex64_early(phys_start);
        uart_puts_early("\n");
    }
}

// ... existing code ...

// Function to ensure vector table is executable at the L3 table level
void ensure_vector_table_executable_l3(uint64_t* l3_table) {
    // Get the vector table address
    uint64_t vbar_addr = read_vbar_el1();
    uint64_t vbar_virt = vbar_addr;
    
    // Calculate the L3 index
    uint64_t l3_idx = (vbar_virt >> 12) & 0x1FF;
    
    // Check if the entry exists
    if (!(l3_table[l3_idx] & PTE_VALID)) {
        uart_puts_early("[VMM] ERROR: Vector table page table entry not valid!\n");
        return;
    }
    
    // Make sure the entry is executable (clear PXN bit)
    uint64_t current_pte = l3_table[l3_idx];
    
    // If PXN bit is set, clear it to allow execution
    if (current_pte & PTE_PXN) {
        uint64_t new_pte = current_pte & ~PTE_PXN;
        
        // Cache maintenance before updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Update the PTE
        l3_table[l3_idx] = new_pte;
        
        // Cache maintenance after updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Invalidate TLB for this address
        asm volatile("tlbi vaae1is, %0" :: "r"(vbar_virt >> 12) : "memory");
        asm volatile("dsb ish" ::: "memory");
        asm volatile("isb" ::: "memory");
        
        uart_puts_early("[VMM] Made vector table executable: 0x");
        uart_hex64_early(vbar_virt);
        uart_puts_early(" PTE: 0x");
        uart_hex64_early(new_pte);
        uart_puts_early("\n");
    } else {
        if (debug_vmm) {
            uart_puts_early("[VMM] Vector table is already executable: 0x");
            uart_hex64_early(vbar_virt);
            uart_puts_early("\n");
        }
    }
}

// ... existing code ...

// Function to audit memory mappings for debugging purposes
void audit_memory_mappings(void) {
    uart_puts_early("[VMM] Auditing memory mappings:\n");
    
    for (int i = 0; i < num_mappings; i++) {
        uart_puts_early("  - ");
        uart_puts_early(mappings[i].name);
        uart_puts_early(": VA 0x");
        uart_hex64_early(mappings[i].virt_start);
        uart_puts_early(" - 0x");
        uart_hex64_early(mappings[i].virt_end);
        uart_puts_early(", PA 0x");
        uart_hex64_early(mappings[i].phys_start);
        uart_puts_early(", Flags 0x");
        uart_hex64_early(mappings[i].flags);
        uart_puts_early("\n");
        
        // Verify the mapping by checking the PTE
        uint64_t pte = get_pte(mappings[i].virt_start);
        uart_puts_early("    PTE: 0x");
        uart_hex64_early(pte);
        
        // Check if the mapping is valid
        if (!(pte & PTE_VALID)) {
            uart_puts_early(" [INVALID]");
        }
        
        // Check if the physical address matches
        uint64_t pte_phys = pte & PTE_ADDR_MASK;
        if (pte_phys != (mappings[i].phys_start & PTE_ADDR_MASK)) {
            uart_puts_early(" [MISMATCH: Expected PA 0x");
            uart_hex64_early(mappings[i].phys_start & PTE_ADDR_MASK);
            uart_puts_early("]");
        }
        
        uart_puts_early("\n");
    }
    
    uart_puts_early("[VMM] Memory audit complete\n");
}

// Implementation of the VMM initialization
void init_vmm_impl(void) {
    uart_puts_early("[VMM] Initializing virtual memory manager (implementation)\n");
    
    // Initialize page tables
    uint64_t* kernel_l0_table = init_page_tables();
    if (!kernel_l0_table) {
        uart_puts_early("[VMM] Failed to initialize page tables\n");
        return;
    }
    
    // Store the kernel page table in the global variable
    l0_table = kernel_l0_table;
    
    // Map the UART
    map_uart();
    
    // Map the kernel sections (uses the global l0_table)
    map_kernel_sections();
    
    // Map the vector table
    map_vector_table();
    
    // Map the MMU transition code
    map_mmu_transition_code();
    
    // Enable MMU
    enable_mmu_enhanced(l0_table);
    
    // MMU is now enabled, and execution continues at mmu_continuation_point
    // which is implemented elsewhere in this file
}

// Initialize the page tables for the kernel
uint64_t* init_page_tables(void) {
    uart_puts_early("[VMM] Initializing page tables\n");
    
    // Allocate L0 table (512 entries, 4KB)
    uint64_t* l0_table = (uint64_t*)alloc_page(); // Use alloc_page instead of pmm_alloc_page
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: Failed to allocate L0 page table\n");
        return NULL;
    }
    
    // Clear the table
    for (int i = 0; i < 512; i++) {
        l0_table[i] = 0;
    }
    
    // Cache maintenance for the L0 table
    for (uintptr_t addr = (uintptr_t)l0_table; 
         addr < (uintptr_t)l0_table + 4096; 
         addr += 64) {
        asm volatile("dc civac, %0" :: "r"(addr) : "memory");
    }
    asm volatile("dsb ish" ::: "memory");
    
    uart_puts_early("[VMM] L0 table created at 0x");
    uart_hex64_early((uint64_t)l0_table);
    uart_puts_early("\n");
    
    return l0_table;
}

// Map the kernel sections (.text, .rodata, .data, etc.)
void map_kernel_sections(void) {
    uart_puts_early("[VMM] Mapping kernel sections\n");
    
    // Use the global kernel L0 table
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: L0 table is NULL in map_kernel_sections\n");
        return;
    }
    
    // Defined in the linker script - use the same declaration style as elsewhere in the file
    extern void* __text_start;
    extern void* __text_end;
    extern char __rodata_start, __rodata_end;
    extern char __data_start, __data_end;
    extern char __bss_start, __bss_end;
    
    // Map kernel text section (.text) as read-only executable
    uart_puts_early("[VMM] Mapping kernel .text section: 0x");
    uart_hex64_early((uint64_t)&__text_start);
    uart_puts_early(" - 0x");
    uart_hex64_early((uint64_t)&__text_end);
    uart_puts_early("\n");
    
    map_range(l0_table, 
              (uint64_t)&__text_start, 
              (uint64_t)&__text_end, 
              (uint64_t)&__text_start, 
              PTE_KERN_TEXT);
    
    // Map kernel read-only data section (.rodata) as read-only non-executable
    uart_puts_early("[VMM] Mapping kernel .rodata section: 0x");
    uart_hex64_early((uint64_t)&__rodata_start);
    uart_puts_early(" - 0x");
    uart_hex64_early((uint64_t)&__rodata_end);
    uart_puts_early("\n");
    
    map_range(l0_table, 
              (uint64_t)&__rodata_start, 
              (uint64_t)&__rodata_end, 
              (uint64_t)&__rodata_start, 
              PTE_KERN_RODATA);
    
    // Map kernel data section (.data) as read-write non-executable
    uart_puts_early("[VMM] Mapping kernel .data section: 0x");
    uart_hex64_early((uint64_t)&__data_start);
    uart_puts_early(" - 0x");
    uart_hex64_early((uint64_t)&__data_end);
    uart_puts_early("\n");
    
    map_range(l0_table, 
              (uint64_t)&__data_start, 
              (uint64_t)&__data_end, 
              (uint64_t)&__data_start, 
              PTE_KERN_DATA);
    
    // Map kernel BSS section (.bss) as read-write non-executable
    uart_puts_early("[VMM] Mapping kernel .bss section: 0x");
    uart_hex64_early((uint64_t)&__bss_start);
    uart_puts_early(" - 0x");
    uart_hex64_early((uint64_t)&__bss_end);
    uart_puts_early("\n");
    
    map_range(l0_table, 
              (uint64_t)&__bss_start, 
              (uint64_t)&__bss_end, 
              (uint64_t)&__bss_start, 
              PTE_KERN_DATA);
    
    uart_puts_early("[VMM] Kernel sections mapped successfully\n");
}

// Enhanced function to enable the MMU with improved robustness
void enable_mmu_enhanced(uint64_t* page_table_base) {
    uart_puts_early("[VMM] Enabling MMU with enhanced protections\n");
    
    // Save the physical address of the page table base
    uint64_t page_table_phys = (uint64_t)page_table_base;
    
    // Save the current vector table address before MMU transition
    uint64_t vbar_el1_addr = read_vbar_el1();
    saved_vector_table_addr = vbar_el1_addr;
    
    uart_puts_early("[VMM] Vector table at physical address: 0x");
    uart_hex64_early(vbar_el1_addr);
    uart_puts_early("\n");
    
    // Set up MAIR_EL1 (Memory Attribute Indirection Register)
    // Attr0: Device-nGnRnE (non-cacheable device) for device memory (00000000)
    // Attr1: Normal, Inner/Outer Write-Back, Read/Write allocate (11111111)
    // Attr2: Normal, non-cacheable (01000100)
    // Attr3: Device-nGnRE (non-cacheable device, but allows early write ack) (00000100)
    uint64_t mair = (MAIR_ATTR_DEVICE_nGnRnE << (8 * ATTR_IDX_DEVICE_nGnRnE)) |
                    (MAIR_ATTR_NORMAL << (8 * ATTR_IDX_NORMAL)) |
                    (MAIR_ATTR_NORMAL_NC << (8 * ATTR_IDX_NORMAL_NC)) |
                    (MAIR_ATTR_DEVICE_nGnRE << (8 * ATTR_IDX_DEVICE_nGnRE));
    
    // Debug: Output MAIR for verification
    uart_puts_early("[VMM] MAIR_EL1 value: 0x");
    uart_hex64_early(mair);
    uart_puts_early("\n");
    
    // Set MAIR_EL1
    MSR("mair_el1", mair);
    
    // Set TCR_EL1 (Translation Control Register)
    // T0SZ[5:0] = 25 (39-bit VA space for TTBR0_EL1)
    // T1SZ[21:16] = 25 (39-bit VA space for TTBR1_EL1)
    // TG0[15:14] = 0 (4KB granule for TTBR0_EL1)
    // TG1[31:30] = 2 (4KB granule for TTBR1_EL1)
    // SH0[13:12] = 3 (Inner shareable)
    // SH1[29:28] = 3 (Inner shareable)
    // ORGN0[11:10] = 1 (Outer Write-Back, Read/Write Allocate)
    // ORGN1[27:26] = 1 (Outer Write-Back, Read/Write Allocate)
    // IRGN0[9:8] = 1 (Inner Write-Back, Read/Write Allocate)
    // IRGN1[25:24] = 1 (Inner Write-Back, Read/Write Allocate)
    // EPD0 = 0 (Enable TTBR0 walks)
    // EPD1 = 0 (Enable TTBR1 walks)
    // IPS[34:32] = 1 (40-bit physical address size)
    uint64_t tcr = (1ULL << 20) |    // TBI0=1: Top Byte Ignored for TTBR0
                   (1ULL << 23) |    // TBI1=1: Top Byte Ignored for TTBR1
                   (25ULL << 0) |    // T0SZ=25: 39-bit VA space for TTBR0
                   (25ULL << 16) |   // T1SZ=25: 39-bit VA space for TTBR1
                   (0ULL << 14) |    // TG0=0: 4KB granule for TTBR0
                   (2ULL << 30) |    // TG1=2: 4KB granule for TTBR1
                   (3ULL << 12) |    // SH0=3: Inner shareable
                   (3ULL << 28) |    // SH1=3: Inner shareable
                   (1ULL << 10) |    // ORGN0=1: Outer Write-Back, Read/Write Allocate
                   (1ULL << 26) |    // ORGN1=1: Outer Write-Back, Read/Write Allocate
                   (1ULL << 8) |     // IRGN0=1: Inner Write-Back, Read/Write Allocate
                   (1ULL << 24) |    // IRGN1=1: Inner Write-Back, Read/Write Allocate
                   (1ULL << 32);     // IPS=1: 40-bit physical address size
    
    // Debug: Output TCR for verification
    uart_puts_early("[VMM] TCR_EL1 value: 0x");
    uart_hex64_early(tcr);
    uart_puts_early("\n");
    
    // Set TCR_EL1
    MSR("tcr_el1", tcr);
    
    // Set TTBR0_EL1 (Translation Table Base Register 0)
    // This is the base address of the L0 page table for user address space
    // For simplicity, we're not setting up user space yet
    MSR("ttbr0_el1", page_table_phys);
    
    // Set TTBR1_EL1 (Translation Table Base Register 1)
}

// ... existing code ...
// ... existing code ...

// Verify a page mapping exists

// Add this after existing function prototypes near the top of the file
void flush_cache_lines(void* addr, size_t size);

// Add this function implementation somewhere in the file, after the declaration of PAGE_SIZE
// Helper function to flush cache lines for a memory region
void flush_cache_lines(void* addr, size_t size) {
    // Calculate address ranges aligned to cache line size (typically 64 bytes)
    uintptr_t start = (uintptr_t)addr & ~(64-1);
    uintptr_t end = ((uintptr_t)addr + size + 64-1) & ~(64-1);
    
    // Flush each cache line in the range
    for (uintptr_t p = start; p < end; p += 64) {
        asm volatile("dc cvac, %0" :: "r"(p) : "memory");
    }
    
    // Data Synchronization Barrier to ensure completion
    asm volatile("dsb ish" ::: "memory");
}

