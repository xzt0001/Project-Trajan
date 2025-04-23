#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"

// Define required constants at the top of the file
#define PAGE_SHIFT 12
#define ENTRIES_PER_TABLE 512
#define PTE_PAGE 3

// Define bool type since we can't include stdbool.h
typedef int bool;
#define true 1
#define false 0

// Structure to hold both virtual and physical addresses for page tables
typedef struct {
    uint64_t* virt;
    uint64_t  phys;
} PageTableRef;

// Global variables
static int mmu_enabled = 0;
static uint64_t* l0_table = NULL;
// Make this variable accessible from other files
uint64_t saved_vector_table_addr = 0; // Added to preserve vector table address

// Debug flag - define at the top before it's used
static bool debug_vmm = false;

// Function to safely write to physical memory with proper cache maintenance
static inline void write_phys64(uint64_t phys_addr, uint64_t value) {
    // Debug output
    uart_puts("[WRITE_PHYS64] Writing 0x");
    uart_hex64(value);
    uart_puts(" to address 0x");
    uart_hex64(phys_addr);
    uart_puts("\n");
    
    // Synchronize before the write
    asm volatile("dsb ish");
    
    // Perform the actual write using a volatile pointer
    *((volatile uint64_t*)phys_addr) = value;
    
    // Clean D-cache to point of coherency for this address
    asm volatile("dc cvac, %0" :: "r"(phys_addr) : "memory");
    
    // Data Synchronization Barrier - ensure cleaning is complete
    asm volatile("dsb ish");
    
    // Instruction Synchronization Barrier - ensure instruction stream sees changes
    asm volatile("isb");
    
    // Verify the write was successful
    uint64_t readback = *((volatile uint64_t*)phys_addr);
    if (readback != value) {
        uart_puts("[WRITE_PHYS64] ERROR: Verification failed! Read 0x");
        uart_hex64(readback);
        uart_puts(" but expected 0x");
        uart_hex64(value);
        uart_puts("\n");
        
        // Try one more time with a different approach
        *((volatile uint64_t*)phys_addr) = value;
        
        // More aggressive cache maintenance
        asm volatile(
            "dc civac, %0\n"  // Clean & Invalidate D-cache by VA to PoC
            "dsb sy\n"        // Full system DSB
            "isb\n"           // Instruction synchronization barrier
            :: "r"(phys_addr) : "memory"
        );
        
        // Verify again
        readback = *((volatile uint64_t*)phys_addr);
        if (readback != value) {
            uart_puts("[WRITE_PHYS64] CRITICAL: Second attempt failed!\n");
        } else {
            uart_puts("[WRITE_PHYS64] Second attempt succeeded\n");
        }
    } else {
        uart_puts("[WRITE_PHYS64] Verification passed\n");
    }
}

// Forward declaration of map_page_direct
void map_page_direct(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);

// Virtual memory system constants
#define PAGE_SIZE           4096    // Standard 4KB page size (ARM64 granule size)
#define PAGE_TABLE_ENTRIES  512     // Entries per table (4096 bytes / 8-byte entries)

// Hardware address definitions
#define KERNEL_VBASE    0x80000     // Kernel virtual base address
#define UART_BASE       0x09000000  // UART base address
#define GIC_DIST_BASE   0x08000000  // GIC Distributor base address
#define GIC_CPU_BASE    0x08010000  // GIC CPU Interface base address 
#define TIMER_BASE      0x08020000  // Timer base address
#define STACK_START     0x40800000  // Stack start address
#define STACK_END       0x40900000  // Stack end address

// Page Table Entry attribute masks
#define PTE_VALID       (1UL << 0)  // Entry is valid
#define PTE_TABLE       (1UL << 1)  // Entry points to a table (vs block)
#define PTE_AF          (1UL << 10) // Access flag - set when page is accessed
#define PTE_SH_INNER    (1UL << 8)  // Inner Shareable (corrected from 3UL to 1UL)
#define PTE_SH_OUTER    (2UL << 8)  // Outer Shareable
#define PTE_SH_NONE     (0UL << 8)  // Non-shareable
#define PTE_TABLE_ADDR  (~0xFFFUL)  // Address mask for table entries (bits [47:12])

// Page Table Entry Flags (ARMv8 architecture)
#define PTE_AP_MASK     (3UL << 6)  // Access Permission mask
#define PTE_AP_RW       (0UL << 6)  // Kernel RW, EL0 no access
#define PTE_AP_RO       (2UL << 6)  // Kernel RO, EL0 no access
#define PTE_AP_USER     (1UL << 6)  // User access bit (when combined with RW/RO)
#define PTE_ATTRINDX(n) ((n) << 2)  // Memory attribute index
#define PTE_NORMAL_NC   PTE_ATTRINDX(1)   // Normal memory, non-cacheable
#define PTE_NORMAL      PTE_ATTRINDX(2)   // Normal memory
#define PTE_DEVICE      PTE_ATTRINDX(0)   // Device memory
#define PTE_KERN_RW     (PTE_AF | PTE_SH_INNER | PTE_AP_RW)
#define PTE_KERN_RO     (PTE_AF | PTE_SH_INNER | PTE_AP_RO)
#define PTE_USER_RW     (PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_AP_USER)
#define PTE_USER_RO     (PTE_AF | PTE_SH_INNER | PTE_AP_RO | PTE_AP_USER)
#define PTE_UXN         (1UL << 54) // Unprivileged Execute Never
#define PTE_PXN         (1UL << 53) // Privileged Execute Never
#define PTE_NOEXEC      (PTE_UXN | PTE_PXN)  // No execution at any level
#define PTE_EXEC        (0UL)           // Executable at all levels (both UXN/PXN clear)

// Define kernel executable memory attributes
#define PTE_KERNEL_EXEC (PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_EXEC) // Kernel executable memory
#define PTE_KERNEL_DATA (PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_NOEXEC) // Kernel data memory (non-executable)
#define PTE_ACCESS PTE_AF // Simplified alias for the Access Flag

// Permission flag inverses - explicitly define to make code more readable
#define PTE_PXN_DISABLE (0UL)       // Allow privilege execution (kernel can execute)  
#define PTE_UXN_DISABLE (0UL)       // Allow unprivileged execution (user can execute)

// Memory Attribute Indirection Register (MAIR) indices
#define ATTR_NORMAL_IDX    0   // Index for normal memory (Attr0 in MAIR_EL1)
#define ATTR_DEVICE_IDX    1   // Index for device memory (Attr1 in MAIR_EL1)

// Memory attributes encoded for page table entries
#define ATTR_NORMAL       (ATTR_NORMAL_IDX << 2)
#define ATTR_DEVICE       (ATTR_DEVICE_IDX << 2)

// New memory attributes - EXECUTABLE normal memory (for code sections)
#define ATTR_NORMAL_EXEC  (ATTR_NORMAL_IDX << 2)  // Normal memory with execution permitted

// Complete flag combination for kernel executable memory
// This explicitly clears PXN/UXN bits for `.text` to make it executable

// Shareable attributes - bits 8-9
// Define PTE_SH_OUTER before using it
#define PTE_SH_OUTER   (2UL << 8)  // Outer Shareable

// Debug UART for direct output even when system is unstable
#define DEBUG_UART 0x09000000

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

// Add missing attribute index definitions at the top of the file with other defines
#define ATTR_IDX_DEVICE_nGnRnE 0
#define ATTR_IDX_NORMAL 1
#define ATTR_IDX_NORMAL_NC 2

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
        uart_puts("[VMM] Error: L3 table is NULL in map_page\n");
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
        uart_puts("[VMM] Mapped VA 0x");
        uart_hex64(va);
        uart_puts(" to PA 0x");
        uart_hex64(pa);
        uart_puts(" with flags 0x");
        uart_hex64(flags);
        uart_puts(" at L3 index ");
        uart_hex64(l3_index);
        uart_puts("\n");
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
    uint64_t vaddr, paddr;
    
    // Calculate physical address offset from virtual
    uint64_t offset = phys_start - virt_start;
    
    // Map each page in the range
    for (vaddr = virt_start; vaddr < virt_end; vaddr += 4096) {
        paddr = vaddr + offset;
        
        // Get the L3 table for this address
        uint64_t* l3_table = get_l3_table_for_addr(l0_table, vaddr);
        
        // Map the page
        map_page(l3_table, vaddr, paddr, flags);
    }
}

// Kernel stack configuration
#define KERNEL_STACK_VA 0x400FF000  // Fixed virtual address for kernel stack
#define KERNEL_STACK_PATTERN 0xDEADBEEF

// External kernel function symbols
extern void task_a(void);
extern void known_alive_function(void);
extern void* vector_table;

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
    extern void* vector_table;
    uint64_t vt_addr = (uint64_t)vector_table;
    uint64_t current_vbar = 0;
    uint64_t phys_vt_addr = 0x00089800; // Physical address of vector table from logs
    
    // First check if VBAR_EL1 is already set
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    
    uart_puts("[VMM] Vector table symbol address: 0x");
    uart_hex64(vt_addr);
    uart_puts("\n[VMM] Current VBAR_EL1: 0x");
    uart_hex64(current_vbar);
    uart_puts("\n");
    
    uart_puts("[VMM] CRITICAL: Explicitly mapping vector table from physical 0x00089800 to virtual 0x1000000\n");
    
    // STEP 1: Direct mapping from physical 0x00089800 to virtual 0x1000000
    map_kernel_page(0x1000000, phys_vt_addr, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
    
    // Also map the second page as vector table is 2KB
    map_kernel_page(0x1000000 + 0x1000, phys_vt_addr + 0x1000, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
    
    // Set the fixed vector table address regardless of the symbol's actual address
    // This is critical - we're using a fixed address for VBAR_EL1
    saved_vector_table_addr = 0x1000000;
    
    uart_puts("[VMM] Vector table explicitly mapped at VA 0x1000000 <- PA 0x00089800\n");
    
    // Verify the mapping
    verify_page_mapping(0x1000000);
    
    // Try to access the vector table to make sure it's accessible
    uart_puts("[VMM] Attempting to read first bytes at 0x1000000...\n");
    volatile uint32_t* vt = (volatile uint32_t*)0x1000000;
    uint32_t first_word = *vt;  // Read the first word of the vector table
    uart_puts("[VMM] First word at vector table: 0x");
    uart_hex64(first_word);
    uart_puts("\n");
    
    // If VBAR_EL1 was already set, update it to point to our new location
    if (current_vbar != 0 && current_vbar != 0x1000000) {
        uart_puts("[VMM] Updating VBAR_EL1 from 0x");
        uart_hex64(current_vbar);
        uart_puts(" to 0x1000000\n");
        
        // Set VBAR_EL1 to our mapped address
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(0x1000000UL)
        );
        
        // Verify it was set
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        uart_puts("[VMM] VBAR_EL1 now: 0x");
        uart_hex64(current_vbar);
        uart_puts("\n");
    }
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
    
    uart_puts("[VMM] Enabling MMU with vector table mapped at 0x");
    uart_hex64(saved_vector_table_addr);
    uart_puts("\n");
    
    // CRITICAL: Verify VBAR_EL1 is set before enabling MMU
    uint64_t current_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
    uart_puts("[VMM] PRE-MMU VBAR_EL1: 0x");
    uart_hex64(current_vbar);
    uart_puts("\n");
    
    // If VBAR_EL1 is not set or doesn't match our mapped address, set it now
    if (current_vbar == 0 || (saved_vector_table_addr != 0 && current_vbar != saved_vector_table_addr)) {
        uint64_t target_vbar = (saved_vector_table_addr != 0) ? saved_vector_table_addr : (uint64_t)&vector_table;
        uart_puts("[VMM] Setting VBAR_EL1 to 0x");
        uart_hex64(target_vbar);
        uart_puts(" before enabling MMU\n");
        
        // Set VBAR_EL1 
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(target_vbar)
        );
        
        // Verify
        asm volatile("mrs %0, vbar_el1" : "=r"(current_vbar));
        uart_puts("[VMM] VBAR_EL1 verification: 0x");
        uart_hex64(current_vbar);
        uart_puts("\n");
    }
    
    // Full MMU initialization sequence for ARMv8-A architecture with immediate VBAR_EL1 update
    asm volatile (
        // 1. Ensure all previous memory accesses complete
        "dsb ish\n"  // Data Synchronization Barrier (Inner Shareable domain)

        // 2. Configure memory attributes (MAIR_EL1)
        // ------------------------------------------
        // Attr0 = 0b11111111 (Normal memory, Write-Back cacheable)
        // Attr1 = 0b00000100 (Device-nGnRE memory)
        "mov x0, #0xFF\n"            // Set MAIR[0] = 0xFF (lower 8 bits)
        "movk x0, #0x44, lsl #16\n"  // Set MAIR[2] = 0x44 (bits 16-23)
        "msr mair_el1, x0\n"         // Write to Memory Attribute Indirection Register

        // 3. Configure translation control (TCR_EL1)
        // ------------------------------------------
        // TG0 = 00 (4KB granule size)
        // T0SZ = 16 (64 - 16 = 48-bit virtual address space)
        // IPS = 001 (36-bit physical address space)
        "mov x0, #0x19\n"            // T0SZ[5:0] = 0x19 (25), but actually 16 due to bit math
        "movk x0, #0x1, lsl #32\n"   // IPS[2:0] = 0b001 (bits 32-34)
        "msr tcr_el1, x0\n"          // Write to Translation Control Register

        // 4. Set page table base address
        // ------------------------------
        "msr ttbr0_el1, %0\n"        // TTBR0_EL1 = page_table_base (user space)
        "msr ttbr1_el1, xzr\n"       // TTBR1_EL1 = 0 (disable higher-half kernel mappings)

        // 5. Ensure system sees register updates
        "isb\n"                      // Instruction Synchronization Barrier

        // 6. Enable MMU
        // -------------
        "mrs x0, sctlr_el1\n"        // Read System Control Register
        "orr x0, x0, #1\n"           // Set M bit (bit 0) to enable MMU
        "msr sctlr_el1, x0\n"        // Write back modified SCTLR_EL1
        "isb\n"                      // Final barrier after MMU enable

        // STEP 2: CRITICAL FIX - Set VBAR_EL1 immediately after MMU enable
        // This must happen before any other code that might cause an exception
        "mov x0, #0x1000000\n"       // Load the fixed virtual address 0x1000000
        "msr vbar_el1, x0\n"         // Set VBAR_EL1 to virtual address
        "isb\n"                      // Ensure VBAR_EL1 is set before continuing
        
        : // No outputs
        : "r"(page_table_base)       // %0: page table root (L0) address
        : "x0"                       // Clobbered register
    );
    
    // DEBUG PATCH: Add prominent marker to show post-MMU VBAR_EL1 value
    uart_puts("\n***** POST-MMU DEBUG *****\n");
    uart_puts("VBAR_EL1 after MMU enabled = 0x");
    uint64_t post_mmu_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(post_mmu_vbar));
    uart_hex64(post_mmu_vbar);
    
    if (post_mmu_vbar == 0x1000000) {
        uart_puts(" [CORRECT - Virtual Address]\n");
    } else if (post_mmu_vbar == 0x00089800) {
        uart_puts(" [ERROR - Still Physical Address]\n");
    } else if (post_mmu_vbar == 0) {
        uart_puts(" [ERROR - Zero Address]\n");
    } else {
        uart_puts(" [UNKNOWN Address]\n");
    }
    uart_puts("**************************\n\n");
    
    // Update global MMU state
    mmu_enabled = 1;  // Atomic flag set (assuming single-core)
    
    uart_puts("[VMM] MMU enabled with VBAR_EL1 set to virtual address 0x1000000\n");
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
    extern void mmu_continuation_point(void);
    uint64_t mmu_cont_addr = (uint64_t)mmu_continuation_point;
    uint64_t page_aligned_addr = mmu_cont_addr & ~0xFFF; // Page-aligned address
    
    // Identity mapping: Use same virtual and physical addresses
    uart_puts("[VMM] CRITICAL: Creating identity mapping for MMU transition code\n");
    uart_puts("[VMM] mmu_continuation_point at address: 0x");
    uart_hex64(mmu_cont_addr);
    uart_puts("\n");
    
    // Map the physical pages containing mmu_continuation_point to the same virtual addresses
    // This ensures that when MMU is enabled, the code continues executing from the same address
    
    // Map the page containing the function with executable permissions
    map_kernel_page(page_aligned_addr, page_aligned_addr, 
                   PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
    
    // Also map the next page in case the function spans page boundaries
    map_kernel_page(page_aligned_addr + 0x1000, page_aligned_addr + 0x1000, 
                   PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
    
    uart_puts("[VMM] Identity mapping created for MMU transition code\n");
    
    // Verify the mapping
    verify_page_mapping(mmu_cont_addr);
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
    uart_puts("[VMM] Initializing virtual memory system\n");
    
    // Initialize L0 (top-level) page table
    l0_table = create_page_table();
    if (!l0_table) {
        uart_puts("[VMM] ERROR: Failed to create L0 page table\n");
        return;
    }
    
    // Create identity mappings for essential regions
    // Map UART at physical 0x09000000
    map_kernel_page(0x09000000, 0x09000000, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_DEVICE);
    
    // Identity map kernel text section (0x80000-0x100000)
    for (uint64_t addr = 0x80000; addr < 0x100000; addr += PAGE_SIZE) {
        map_kernel_page(addr, addr, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
    }
    
    // Identity map kernel data/rodata section (0x100000-0x200000)
    for (uint64_t addr = 0x100000; addr < 0x200000; addr += PAGE_SIZE) {
        map_kernel_page(addr, addr, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL);
    }
    
    // Map stack region (0x40800000)
    for (uint64_t addr = 0x40800000; addr < 0x40900000; addr += PAGE_SIZE) {
        map_kernel_page(addr, addr, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL);
    }
    
    // Map vector table
    map_vector_table();
    
    // Identity map MMU transition code
    map_mmu_transition_code();
    
    // Enable MMU
    enable_mmu(l0_table);
    
    uart_puts("[VMM] Virtual memory system initialization complete\n");
}

// Return the kernel's L0 (top-level) page table
uint64_t* get_kernel_page_table(void) {
    return l0_table;
}

// Continuation point for after MMU is enabled
void __attribute__((used, aligned(4096))) mmu_continuation_point(void) {
    uart_puts("\n>>> MMU transition SUCCESSFUL! <<<\n");

    uint64_t post_mmu_vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(post_mmu_vbar));
    uart_puts("VBAR_EL1 = 0x");
    uart_hex64(post_mmu_vbar);

    // Classify the result
    if (post_mmu_vbar == 0x1000000) {
        uart_puts(" [OK: Virtual Address]\n");
    } else if (post_mmu_vbar == 0x00089800) {
        uart_puts(" [ERROR: Still Physical Address]\n");
    } else if (post_mmu_vbar == 0) {
        uart_puts(" [ERROR: Zero Address]\n");
    } else {
        uart_puts(" [Unknown VBAR_EL1 value]\n");
    }

    uart_puts("********************************\n");

    // Slow it down to ensure UART prints
    for (volatile int i = 0; i < 1000000; i++);
    
    // Now it's safe to enable caches
    asm volatile(
        "mrs x0, sctlr_el1\n"
        "orr x0, x0, #(1 << 12)\n"  // Set I bit (instruction cache)
        "orr x0, x0, #(1 << 2)\n"   // Set C bit (data cache)
        "msr sctlr_el1, x0\n"
        "isb\n"
        ::: "x0"
    );
    
    // Final confirmation
    uart_puts("[MMU] Successfully enabled with caches activated!\n");
}

// Ensure that code sections have executable permissions
void ensure_code_is_executable(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'F'; *uart = 'I'; *uart = 'X'; *uart = 'X'; *uart = ':'; // Fix execute permissions
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        *uart = 'L'; *uart = '0'; *uart = '!'; // L0 error
        return;
    }
    
    // Check and fix permissions for key functions
    uart_puts("[VMM] Ensuring code sections are executable\n");
    
    // Fix the vector table permissions
    ensure_vector_table_executable();
    
    // Verify executable permissions for key code regions
    extern void dummy_asm(void);
    extern void test_scheduler(void);
    extern void known_branch_test(void);
    extern void full_restore_context(void*);
    
    // Verify executable addresses
    verify_executable_address(l0_table, (uint64_t)&dummy_asm, "dummy_asm");
    verify_executable_address(l0_table, (uint64_t)&test_scheduler, "test_scheduler");
    verify_executable_address(l0_table, (uint64_t)&known_branch_test, "known_branch_test");
    verify_executable_address(l0_table, (uint64_t)&full_restore_context, "full_restore_context");
    
    uart_puts("[VMM] Code sections verified as executable\n");
}

// Basic initialization of the VMM
void init_vmm_impl(void) {
    uart_puts("[VMM] Initializing virtual memory system impl\n");
    
    // Create the L0 table (top level)
    l0_table = create_page_table();
    if (!l0_table) {
        uart_puts("[VMM] ERROR: Failed to create L0 table\n");
        return;
    }
    
    uart_puts("[VMM] L0 table @ 0x");
    uart_hex64((uint64_t)l0_table);
    uart_puts("\n");
    
    // Create L1 table for first 512GB chunk
    uint64_t* l1_table = create_page_table();
    if (!l1_table) {
        uart_puts("[VMM] ERROR: Failed to create L1 table\n");
        return;
    }
    
    uart_puts("[VMM] L1 table @ 0x");
    uart_hex64((uint64_t)l1_table);
    uart_puts("\n");
    
    // Create L2 table for first 1GB chunk
    uint64_t* l2_table = create_page_table();
    if (!l2_table) {
        uart_puts("[VMM] ERROR: Failed to create L2 table\n");
        return;
    }
    
    uart_puts("[VMM] L2 table @ 0x");
    uart_hex64((uint64_t)l2_table);
    uart_puts("\n");
    
    // Create L3 table for first 2MB chunk
    uint64_t* l3_table = create_page_table();
    if (!l3_table) {
        uart_puts("[VMM] ERROR: Failed to create L3 table\n");
        return;
    }
    
    uart_puts("[VMM] L3 table @ 0x");
    uart_hex64((uint64_t)l3_table);
    uart_puts("\n");
    
    // Link the tables together
    l0_table[0] = ((uint64_t)l1_table) | PTE_VALID | PTE_TABLE;
    l1_table[0] = ((uint64_t)l2_table) | PTE_VALID | PTE_TABLE;
    l2_table[0] = ((uint64_t)l3_table) | PTE_VALID | PTE_TABLE;
    
    // Verify the page table links
    uart_puts("Verifying page table entries:\n");
    uart_puts("L0[0] entry");
    uart_hex64(l0_table[0]);
    uart_puts("\n");
    
    uart_puts("L1[0] entry");
    uart_hex64(l1_table[0]);
    uart_puts("\n");
    
    uart_puts("L2[0] entry");
    uart_hex64(l2_table[0]);
    uart_puts("\n");
    
    // Map the first few pages of physical memory as identity mapping
    for (uint64_t addr = 0; addr < 0x100000; addr += PAGE_SIZE) {
        map_page(l3_table, addr, addr, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL);
    }
    
    // Map UART registers
    map_page(l3_table, 0x09000000, 0x09000000, PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_DEVICE);
    
    uart_puts("[VMM] Basic memory mappings created\n");
}

// Implement the get_pte function to retrieve a page table entry
uint64_t get_pte(uint64_t virt_addr) {
    uint64_t* l0 = get_kernel_page_table();
    uint64_t l0_index = (virt_addr >> 39) & 0x1FF;
    uint64_t l1_index = (virt_addr >> 30) & 0x1FF;
    uint64_t l2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t l3_index = (virt_addr >> 12) & 0x1FF;

    if (!l0 || !(l0[l0_index] & PTE_VALID)) return 0;
    uint64_t* l1 = (uint64_t*)((l0[l0_index] & ~0xFFFUL));
    
    if (!l1 || !(l1[l1_index] & PTE_VALID)) return 0;
    uint64_t* l2 = (uint64_t*)((l1[l1_index] & ~0xFFFUL));
    
    if (!l2 || !(l2[l2_index] & PTE_VALID)) return 0;
    uint64_t* l3 = (uint64_t*)((l2[l2_index] & ~0xFFFUL));
    
    if (!l3 || !(l3[l3_index] & PTE_VALID)) return 0;
    
    return l3[l3_index];
}

// Ensure the vector table memory is executable (L3 table version)
void ensure_vector_table_executable_l3(uint64_t* l3_table) {
    extern void* vector_table;
    uint64_t vector_addr = (uint64_t)&vector_table;
    
    debug_print("[VBAR] Ensuring vector table memory is executable...\n");
    
    // Calculate indices for vector table
    uint64_t vt_addr = vector_addr & ~0xFFF; // Page-aligned address
    uint64_t vt_idx = (vt_addr >> 12) & 0x1FF; // Get L3 index (bits 21:12)
    
    debug_print("[VBAR] Vector table virtual address: 0x");
    debug_hex64("", vt_addr);
    debug_print("\n[VBAR] L3 index for vector table: ");
    uart_putc('0' + (vt_idx / 100) % 10);
    uart_putc('0' + (vt_idx / 10) % 10);
    uart_putc('0' + vt_idx % 10);
    debug_print("\n");
    
    // Check if entry exists (it should, but we'll verify)
    uint64_t pte = l3_table[vt_idx];
    if (!(pte & PTE_VALID)) {
        debug_print("[VBAR] ERROR: No valid mapping exists for vector table!\n");
        
        // Get the physical address for vector_table (identity mapped)
        uint64_t phys_addr = vt_addr; // Assuming identity mapping initially
        
        // Create a mapping with executable permissions
        map_page(l3_table, vt_addr, phys_addr, 
                 PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
        
        debug_print("[VBAR] Created new mapping for vector table\n");
    } else {
        debug_print("[VBAR] Existing mapping found for vector table, modifying flags...\n");
        
        // Existing mapping found, clear the UXN and PXN bits to make it executable
        uint64_t new_pte = pte & ~(PTE_UXN | PTE_PXN);
        l3_table[vt_idx] = new_pte;
        
        debug_print("[VBAR] Updated vector table mapping to be executable\n");
    }
    
    // Also ensure the next page is mapped (vector table spans 2KB)
    uint64_t next_page = vt_addr + 0x1000;
    uint64_t next_idx = (next_page >> 12) & 0x1FF;
    
    pte = l3_table[next_idx];
    if (!(pte & PTE_VALID)) {
        debug_print("[VBAR] No valid mapping for second vector table page!\n");
        
        uint64_t phys_addr = next_page; // Assuming identity mapping initially
        
        map_page(l3_table, next_page, phys_addr, 
                 PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | ATTR_NORMAL_EXEC);
        
        debug_print("[VBAR] Created new mapping for second vector table page\n");
    } else {
        debug_print("[VBAR] Existing mapping found for second vector table page, making executable...\n");
        
        uint64_t new_pte = pte & ~(PTE_UXN | PTE_PXN);
        l3_table[next_idx] = new_pte;
        
        debug_print("[VBAR] Updated second vector table page to be executable\n");
    }
    
    // Flush TLB to ensure changes take effect
    asm volatile (
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory"
    );
    
    debug_print("[VBAR] Vector table memory is now executable\n");
}

// Top-level function to ensure vector table is executable
void ensure_vector_table_executable(void) {
    debug_print("[VBAR] Ensuring vector table is executable (top level)...\n");
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        debug_print("[VBAR] ERROR: Could not get kernel page table!\n");
        return;
    }
    
    // Get the current vector table address
    extern void* vector_table;
    uint64_t vector_addr = (uint64_t)&vector_table;
    
    // Get the L3 table for the vector table address
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, vector_addr);
    if (!l3_table) {
        debug_print("[VBAR] ERROR: Could not get L3 table for vector table address!\n");
        return;
    }
    
    // Call the L3 version to update the PTE
    ensure_vector_table_executable_l3(l3_table);
    
    // Check if VBAR_EL1 is correctly set to the vector table address
    uint64_t vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    
    debug_print("[VBAR] Current VBAR_EL1: 0x");
    debug_hex64("", vbar);
    debug_print("\n[VBAR] Vector table address: 0x");
    debug_hex64("", vector_addr);
    debug_print("\n");
    
    // If VBAR_EL1 is not correctly set, update it
    if (vbar != vector_addr) {
        debug_print("[VBAR] VBAR_EL1 is not correctly set. Updating...\n");
        
        asm volatile(
            "msr vbar_el1, %0\n"
            "isb\n"
            :: "r"(vector_addr)
        );
        
        // Verify the update
        asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
        
        debug_print("[VBAR] Updated VBAR_EL1 to: 0x");
        debug_hex64("", vbar);
        debug_print("\n");
        
        if (vbar == vector_addr) {
            debug_print("[VBAR] VBAR_EL1 successfully updated\n");
        } else {
            debug_print("[VBAR] ERROR: Failed to update VBAR_EL1!\n");
        }
    } else {
        debug_print("[VBAR] VBAR_EL1 is already correctly set\n");
    }
}
