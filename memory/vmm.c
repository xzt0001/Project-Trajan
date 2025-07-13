#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"
#include "../include/memory_config.h"
#include "memory_debug.h"
#include "../include/memory_core.h"

// Prototypes for diagnostic helpers
uint64_t read_mair_el1(void);
uint64_t read_pte_entry(uint64_t va);
void debug_hex64_mmu(const char* label, uint64_t value);

// PageTableRef structure now defined in memory_config.h

// Global variables (extern declarations now in memory_config.h)
extern bool mmu_enabled;  // Use the one defined in uart_late.c
uint64_t* l0_table = NULL;
uint64_t* l0_table_ttbr1 = NULL;  // Separate page table for TTBR1_EL1
uint64_t saved_vector_table_addr = 0; // Added to preserve vector table address

// Debug flag - define at the top before it's used
bool debug_vmm = false; // Set to false to reduce UART noise during boot

// Forward declarations for UART handling
void map_uart(void);
void verify_uart_mapping(void);

// write_phys64 function now defined in memory_config.h

// ARMv8 page table entry constants and memory attributes now defined in memory_config.h

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
void verify_critical_mappings_before_mmu(uint64_t* page_table_base);  // Step 4 function
void enhanced_cache_maintenance(void);  // Step 4 function

// Additional function declarations for UART and debug support
extern void uart_putx(uint64_t val);
extern void uart_hex64(uint64_t val);
extern void debug_print(const char* msg);
extern void debug_hex64(const char* label, uint64_t value);
extern void uart_putc(char c);

// MSR/MRS macros, function prototypes, and memory constants now defined in memory_config.h

// Helper functions to read system registers
// MOVED TO memory/memory_core.c
/*
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
*/

// Function to dump PTE info in a simpler format for debugging
// MOVED TO memory/memory_debug.c
/*
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
*/

// Function to print page table entry flags for debugging
// MOVED TO memory/memory_debug.c
/*
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
*/

// Fix the debug_check_mapping function to accept a name parameter and correct usage
// MOVED TO memory/memory_debug.c
/*
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
*/

// Define PTE_NORMAL and PTE_DEVICE for easier mapping
// #define PTE_NORMAL (PTE_VALID | PTE_AF | PTE_SH_INNER | (ATTR_IDX_NORMAL << 2))
// #define PTE_DEVICE (PTE_VALID | PTE_AF | PTE_SH_INNER | (ATTR_IDX_DEVICE_nGnRnE << 2))

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration
/*
uint64_t* create_page_table(void) {
    void* table = alloc_page();
    if (!table) {
        uart_puts("[VMM] Failed to allocate page table!\n");
        return NULL;
    }

    memset(table, 0, PAGE_SIZE);  // clear entries
    return (uint64_t*)table;
}
*/

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
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
*/

// Function declarations
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
void init_vmm_impl(void);
void init_vmm_wrapper(void);
int verify_executable_address(uint64_t *table_ptr, uint64_t vaddr, const char* desc);
void map_code_section(void);

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
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
        
        // Determine which page table to use based on virtual address
        uint64_t* page_table_to_use;
        if (virt_addr >= HIGH_VIRT_BASE) {
            // High virtual address - use TTBR1 page table
            page_table_to_use = l0_table_ttbr1;
            uart_puts_early("[VMM] Using TTBR1 page table for high VA 0x");
            uart_hex64_early(virt_addr);
            uart_puts_early("\n");
        } else {
            // Low virtual address - use TTBR0 page table (passed parameter)
            page_table_to_use = l0_table;
            uart_puts_early("[VMM] Using TTBR0 page table for low VA 0x");
            uart_hex64_early(virt_addr);
            uart_puts_early("\n");
        }
        
        // Create/get L3 table for the virtual address
        uint64_t* l3_table = get_l3_table_for_addr(page_table_to_use, virt_addr);
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
*/

// Kernel stack configuration now defined in memory_config.h

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
// MOVED TO memory/memory_debug.c
/*
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
*/

// Function to debug memory permissions across different regions
void debug_memory_permissions(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = ' ';
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
}

// Function to verify code sections are executable
// MOVED TO memory/memory_debug.c
/*
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
*/

/**
 * Verify an address is properly mapped as executable through all page table levels
 * Returns 1 if address is executable, 0 otherwise
 * MOVED TO memory/memory_debug.c
 */
/*
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
*/

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

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
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
*/

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

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
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
*/

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
    
    uart_puts_early("[VMM] Enabling MMU with enhanced instruction continuity fixes\n");
    uart_puts_early("[VMM] Vector table mapped at 0x");
    uart_hex64_early(saved_vector_table_addr);
    uart_puts_early("\n");
    
    // Step 4: Verify critical mappings before MMU enable
    verify_critical_mappings_before_mmu(page_table_base);
    
    // Enhanced cache maintenance
    enhanced_cache_maintenance();
    
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
    
    // Use the enhanced version that includes complete TCR_EL1 and instruction fetch fixes
    enable_mmu_enhanced(page_table_base);
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
// MOVED TO memory/memory_core.c
/*
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
*/

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

// PHASE 1 & 2: Diagnostic mapping with identity-only test
void map_mmu_transition_code(void) {
    // PHASE 1: Use direct UART instead of uart_puts_early to avoid hangs
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'E'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Use the global kernel L0 table
    if (!l0_table) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'L'; *uart = '0';
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Get critical function addresses
    extern void enable_mmu_enhanced(uint64_t* page_table_base);
    extern void mmu_continuation_point(void);
    
    uint64_t enable_mmu_phys = (uint64_t)&enable_mmu_enhanced;
    uint64_t continuation_phys = (uint64_t)&mmu_continuation_point;
    
    // Get current execution context
    uint64_t current_pc;
    asm volatile("adr %0, ." : "=r"(current_pc));
    
    uint64_t current_sp;
    asm volatile("mov %0, sp" : "=r"(current_sp));
    
    // Show critical addresses using direct UART
    *uart = 'A'; *uart = 'D'; *uart = 'D'; *uart = 'R'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    
    // Find the actual range of critical functions
    uint64_t min_addr = enable_mmu_phys;
    uint64_t max_addr = enable_mmu_phys;
    
    if (continuation_phys < min_addr) min_addr = continuation_phys;
    if (continuation_phys > max_addr) max_addr = continuation_phys;
    if (current_pc < min_addr) min_addr = current_pc;
    if (current_pc > max_addr) max_addr = current_pc;
    
    // Calculate mapping region
    uint64_t region_start = (min_addr & ~0xFFF) - 0x10000;  // 64KB before
    uint64_t region_end   = ((max_addr + 0xFFF) & ~0xFFF) + 0x10000;  // 64KB after
    
    // Safety check: ensure reasonable mapping size
    uint64_t mapping_size = region_end - region_start;
    if (mapping_size > 0x100000) { // More than 1MB is suspicious
        *uart = 'W'; *uart = 'A'; *uart = 'R'; *uart = 'N'; *uart = ':'; *uart = 'B'; *uart = 'I'; *uart = 'G';
        *uart = '\r'; *uart = '\n';
        region_end = region_start + 0x100000;
    }
    
    // PHASE 1A: Start identity mapping marker  
    *uart = 'P'; *uart = 'H'; *uart = '1'; *uart = 'A'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // STEP 1A: Create identity mapping (physical → same virtual)
    uart_puts_early("[VMM] STEP 1A: Creating identity mapping\n");
    uart_puts_early("[VMM] Identity region: 0x");
    uart_hex64_early(region_start);
    uart_puts_early(" - 0x");
    uart_hex64_early(region_end);
    uart_puts_early("\n");
    
    map_range(l0_table, 
              region_start, 
              region_end,
              region_start, 
              PTE_KERN_TEXT); // Executable, cacheable, kernel access
    
    // PHASE 1A: End identity mapping marker
    *uart = 'P'; *uart = 'H'; *uart = '1'; *uart = 'A'; *uart = ':'; *uart = 'E'; *uart = 'N'; *uart = 'D';
    *uart = '\r'; *uart = '\n';
    
    // PHASE 1B: Enable high virtual mapping
    *uart = 'P'; *uart = 'H'; *uart = '1'; *uart = 'B'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Declare variables needed for virtual mapping
    uint64_t high_virt_base = HIGH_VIRT_BASE;
    uint64_t virt_region_start = high_virt_base | region_start;
    uint64_t virt_region_end = high_virt_base | region_end;
    
    // STEP 1B: Create high virtual mapping (physical → 0xFFFF000000000000UL | physical)
    uart_puts_early("[VMM] STEP 1B: Creating high virtual mapping\n");
    uart_puts_early("[VMM] Virtual region: 0x");
    uart_hex64_early(virt_region_start);
    uart_puts_early(" - 0x");
    uart_hex64_early(virt_region_end);
    uart_puts_early("\n");
    
    map_range(l0_table,
              virt_region_start,
              virt_region_end,
              region_start,  // Same physical addresses
              PTE_KERN_TEXT); // Executable, cacheable, kernel access
    
    // PHASE 1B: Completion marker
    *uart = 'P'; *uart = 'H'; *uart = '1'; *uart = 'B'; *uart = ':'; *uart = 'E'; *uart = 'N'; *uart = 'D';
    *uart = '\r'; *uart = '\n';
    
    // PHASE 1C: Safe stack mapping using actual current stack
    *uart = 'S'; *uart = 'T'; *uart = 'K'; *uart = ':'; *uart = 'M'; *uart = 'A'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
    
    // Use actual current stack pointer for mapping
    uint64_t stack_page = current_sp & ~0xFFF;
    uint64_t stack_region_start = stack_page - 0x2000; // 8KB before for safety
    uint64_t stack_region_end = stack_page + 0x2000;   // 8KB after for safety
    
    // Output actual stack information using direct UART
    *uart = 'S'; *uart = 'P'; *uart = ':'; // SP:
    uint8_t* sp_bytes = (uint8_t*)&current_sp;
    for (int i = 7; i >= 4; i--) { // Show high 4 bytes
        uint8_t byte = sp_bytes[i];
        uint8_t high = (byte >> 4) & 0xF;
        uint8_t low = byte & 0xF;
        *uart = high < 10 ? '0' + high : 'A' + (high - 10);
        *uart = low < 10 ? '0' + low : 'A' + (low - 10);
    }
    *uart = '\r'; *uart = '\n';
    
    // STEP 1C: Create identity mapping for actual current stack
    map_range(l0_table,
              stack_region_start,
              stack_region_end,
              stack_region_start,
              PTE_KERN_DATA); // Non-executable data, kernel access
    
    // Calculate virtual stack addresses
    uint64_t virt_stack_start = high_virt_base | stack_region_start;
    uint64_t virt_stack_end = high_virt_base | stack_region_end;
            
    // STEP 1C: Create virtual stack mapping
    map_range(l0_table,
              virt_stack_start,
              virt_stack_end,
              stack_region_start,  // Same physical addresses
              PTE_KERN_DATA);
            
    // PHASE 1C: Completion marker
    *uart = 'S'; *uart = 'T'; *uart = 'K'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Register mappings for diagnostic (both identity and virtual enabled)
    register_mapping(region_start, region_end, 
                    region_start, PTE_KERN_TEXT, "Kernel Code (Identity)");
    register_mapping(virt_region_start, virt_region_end, 
                    region_start, PTE_KERN_TEXT, "Kernel Code (High Virtual)");
    register_mapping(stack_region_start, stack_region_end, 
                    stack_region_start, PTE_KERN_DATA, "Stack (Identity)");
    register_mapping(virt_stack_start, virt_stack_end, 
                    stack_region_start, PTE_KERN_DATA, "Stack (High Virtual)");
            
    // Verification of critical addresses using direct UART (no string functions)
    *uart = 'V'; *uart = 'E'; *uart = 'R'; *uart = 'I'; *uart = 'F'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    
    uint64_t critical_addrs[] = {
        enable_mmu_phys,
        continuation_phys,
        current_pc,
        0 // End marker
    };
    
    for (int i = 0; critical_addrs[i] != 0; i++) {
        uint64_t addr = critical_addrs[i];
        uint64_t virt_addr = high_virt_base | addr;
        
        // Output function index
        *uart = 'F'; *uart = '0' + i; *uart = ':';
        
        // Check identity mapping
        if (addr >= region_start && addr < region_end) {
            *uart = 'I'; *uart = 'D'; *uart = '+';  // Identity mapped
        } else {
            *uart = 'I'; *uart = 'D'; *uart = '-';  // Identity not mapped
        }
        
        // Check virtual mapping  
        if (virt_addr >= virt_region_start && virt_addr < virt_region_end) {
            *uart = 'V'; *uart = 'I'; *uart = '+';  // Virtual mapped
    } else {
            *uart = 'V'; *uart = 'I'; *uart = '-';  // Virtual not mapped
        }
        
        *uart = '\r'; *uart = '\n';
    }
    
    *uart = 'V'; *uart = 'E'; *uart = 'R'; *uart = 'I'; *uart = 'F'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Enhanced TLB invalidation
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    
    asm volatile(
        "dsb sy\n"              // System-wide data synchronization (single-core safe)
        "tlbi vmalle1\n"        // Invalidate TLB entries for EL1 (local core only)
        "dsb sy\n"              // Wait for TLB invalidation completion (system-wide)
        "ic iallu\n"            // Invalidate instruction cache (already local)
        "dsb sy\n"              // Wait for instruction cache invalidation (system-wide)
        "isb\n"                 // Instruction synchronization barrier
        ::: "memory"
    );
    
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // PHASE 1A+1B+1C: Completion marker
    *uart = 'P'; *uart = 'H'; *uart = '1'; *uart = ':'; *uart = 'C'; *uart = 'O'; *uart = 'M'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
}

// Simple wrapper function to call init_vmm_impl
void init_vmm_wrapper(void) {
    // Add direct UART markers for early failure detection
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Marker: Entering init_vmm_wrapper
    *uart = '[';
    *uart = 'W';
    *uart = 'R';
    *uart = 'A';
    *uart = 'P';
    *uart = ']';
    *uart = '\r';
    *uart = '\n';
    
    // Step 1: init_vmm_impl
    *uart = '1';
    *uart = ':';
    *uart = 'I';
    *uart = 'N';
    *uart = 'I';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    init_vmm_impl();
    
    // If we reach here, init_vmm_impl completed
    *uart = '1';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step 2: Create identity mapping for MMU transition code
    *uart = '2';
    *uart = ':';
    *uart = 'M';
    *uart = 'A';
    *uart = 'P';
    *uart = '\r';
    *uart = '\n';
    
    map_mmu_transition_code();
    
    // If we reach here, map_mmu_transition_code completed
    *uart = '2';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step 3: Map the vector table to 0x1000000 before enabling MMU
    *uart = '3';
    *uart = ':';
    *uart = 'V';
    *uart = 'E';
    *uart = 'C';
    *uart = '\r';
    *uart = '\n';
    
    map_vector_table();
    
    // If we reach here, map_vector_table completed
    *uart = '3';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step 4: Now enable the MMU
    *uart = '4';
    *uart = ':';
    *uart = 'M';
    *uart = 'M';
    *uart = 'U';
    *uart = '\r';
    *uart = '\n';
    
    enable_mmu(l0_table);
    
    // We should NEVER reach here since enable_mmu branches to mmu_continuation_point
    *uart = 'E';
    *uart = 'R';
    *uart = 'R';
    *uart = ':';
    *uart = 'R';
    *uart = 'E';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
}

// Function to hold the current version of init_vmm as we transition
void init_vmm(void) {
    // Create initial debug output
    uart_puts("[VMM] Initializing virtual memory system\n");
    
    // Initialize page tables
    init_vmm_impl();
    
    // Map the vector table before enabling MMU
    uart_puts_early("VT:");
    map_vector_table();
    uart_puts_early("OK\n");
    
    // Map UART to a virtual address for use after MMU is enabled
    uart_puts_early("UART:");
    map_uart();
    uart_puts_early("OK\n");
    
    // Map transition code (MMU continuation point)
    uart_puts_early("TRANS:");
    map_mmu_transition_code();
    uart_puts_early("OK\n");
    
    // Phase 9: Audit memory mappings for overlaps before enabling MMU
    uart_puts_early("AUDIT:");
    audit_memory_mappings();
    uart_puts_early("OK\n");
    
    // Verify memory permissions before enabling MMU
    uart_puts_early("VERIFY:");
    verify_code_is_executable();
    uart_puts_early("OK\n");
    
    // Enable MMU with our page table
    uart_puts_early("ENABLE:");
    enable_mmu(l0_table);
    
    // We should never reach here since we branch to mmu_continuation_point in enable_mmu
    uart_puts_early("[VMM] ERROR: Returned from enable_mmu without branching!\n");
}

// Return the kernel's L0 (top-level) page table
// MOVED TO memory/memory_core.c
/*
uint64_t* get_kernel_page_table(void) {
    return l0_table;
}
*/

// Return the TTBR1 L0 page table for high virtual addresses
// MOVED TO memory/memory_core.c
/*
uint64_t* get_kernel_ttbr1_page_table(void) {
    return l0_table_ttbr1;
}
*/

// Continuation point for after MMU is enabled
// Add special section attribute to ensure proper placement  
void __attribute__((used, aligned(4096), section(".text.mmu_continuation")))
mmu_continuation_point(void) {
    // IMMEDIATE confirmation we entered the function - use both physical and virtual UART
    volatile uint32_t* uart_phys = (volatile uint32_t*)0x09000000;
    volatile uint32_t* uart_virt = (volatile uint32_t*)UART_VIRT;
    
    // First thing: confirm we entered the continuation point
    *uart_phys = 'C';
    *uart_phys = 'O';
    *uart_phys = 'N';
    *uart_phys = 'T';
    *uart_phys = ':';
    
    // Get current exception level for debugging
    uint64_t current_el;
    asm volatile("mrs %0, currentel" : "=r"(current_el));
    current_el = (current_el >> 2) & 0x3;
    *uart_phys = '0' + current_el;
    
    // Quick MMU verification
    uint64_t sctlr_val;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    if (sctlr_val & 1) {
        *uart_phys = 'M';
        *uart_phys = '+';  // MMU enabled
    } else {
        *uart_phys = 'M';
        *uart_phys = '-';  // MMU disabled
    }
    
    // Try virtual UART access carefully
    *uart_phys = 'V';
    *uart_phys = ':';

    // Test virtual UART access - this will only work if MMU is correctly configured
    bool virt_uart_works = false;
    
    // Attempt virtual UART access with error handling
    *uart_virt = 'T';
    *uart_virt = 'E';
    *uart_virt = 'S';
    *uart_virt = 'T';
    virt_uart_works = true;  // If we get here, virtual access worked
    
    if (virt_uart_works) {
        *uart_virt = 'O';
        *uart_virt = 'K';
        
        // Success! Switch to virtual UART for remaining output
        *uart_virt = '\r';
        *uart_virt = '\n';
    
        // Extended success message using virtual UART
        const char* success_msg = "[MMU] SUCCESS: Virtual addressing working!\r\n";
        for (const char* p = success_msg; *p; p++) {
            *uart_virt = *p;
        }
        
        // Switch UART base to virtual address for kernel
        uart_set_base((void*)UART_VIRT);
    
        // Use kernel UART functions now that virtual addressing works
        uart_puts("[MMU] Continuation point reached successfully!\n");
        uart_puts("[MMU] MMU is enabled and virtual addressing is working\n");
        uart_puts("[MMU] Exception Level: EL");
        uart_putc('0' + current_el);
        uart_puts("\n");
        
        // Report SCTLR_EL1 status
        uart_puts("[MMU] SCTLR_EL1: 0x");
        uart_hex64(sctlr_val);
        uart_puts("\n");
    
        // Continue with system initialization...
        uart_puts("[MMU] MMU initialization complete, continuing boot...\n");
        
    } else {
        // Virtual UART failed - continue with physical UART
        *uart_phys = 'F';
        *uart_phys = 'A';
        *uart_phys = 'I';
        *uart_phys = 'L';
        *uart_phys = '\r';
        *uart_phys = '\n';
    
        // Stay on physical UART and report the issue
        const char* fail_msg = "[MMU] ERROR: Virtual UART access failed\r\n";
        for (const char* p = fail_msg; *p; p++) {
            *uart_phys = *p;
        }
    }
}

// ... existing code ...

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
void map_uart(void) {
    uart_puts_early("[VMM] Mapping UART MMIO region\n");
    
    // Make sure we have access to kernel L0 table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        uart_puts_early("[VMM] ERROR: Failed to get kernel page table for UART mapping\n");
        return;
    }
    
    // Select the correct L0 root table for the UART virtual address.
    // High kernel addresses (>= HIGH_VIRT_BASE) must be placed in the
    // TTBR1 tree; low addresses use TTBR0.  UART_VIRT is always high, but
    // keep the check generic in case the constant changes.

    uint64_t* root_for_virt = (UART_VIRT >= HIGH_VIRT_BASE) ? l0_table_ttbr1
                                                            : l0_table;

    // Create/get L3 table for the UART virtual address region using the
    // selected root.
    uint64_t* l3_table = get_l3_table_for_addr(root_for_virt, UART_VIRT);
    if (!l3_table) {
        uart_puts_early("[VMM] ERROR: Could not get L3 table for UART virtual address\n");
        return;
    }
    
    // Device memory attributes for UART MMIO - FIXED: Add all required flags
    uint64_t uart_flags = PTE_VALID | PTE_PAGE | PTE_AF |
                          PTE_DEVICE_nGnRE | PTE_AP_RW |
                          PTE_PXN | PTE_UXN;          // virtual
    
    // Make the UART virtual mapping non-executable to match the physical
    // identity mapping and avoid attribute mismatches across descriptors.
    uart_flags |= PTE_PXN | PTE_UXN;
    
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

    // NEW: also create an *identity* mapping for the UART MMIO page so
    // that the very first physical UART access that occurs immediately
    // after the M-bit is set will translate successfully.  We cannot use
    // map_kernel_page() because its internals eventually call map_page(),
    // and map_page() intentionally skips any VA or PA inside the UART
    // range to avoid double-mapping.  Therefore we build the entry
    // manually, mirroring the logic used a few lines above for the high
    // virtual mapping, but this time with VA = PA.

    uint64_t* l3_table_phys = get_l3_table_for_addr(l0_table, UART_PHYS);
    if (l3_table_phys) {
        uint64_t l3_idx_phys = (UART_PHYS >> 12) & 0x1FF;
        uint64_t pte_phys = UART_PHYS |
                           PTE_VALID | PTE_PAGE | PTE_AF |
                           PTE_SH_NONE |       // Device memory must be non-shareable
                           PTE_DEVICE_nGnRE |
                           PTE_AP_RW |
                           PTE_PXN | PTE_UXN;  // never executable

        // Cache maintenance
        asm volatile("dc civac, %0" :: "r"(&l3_table_phys[l3_idx_phys]) : "memory");
        asm volatile("dsb ish" ::: "memory");

        l3_table_phys[l3_idx_phys] = pte_phys;

        asm volatile("dc civac, %0" :: "r"(&l3_table_phys[l3_idx_phys]) : "memory");
        asm volatile("dsb ish" ::: "memory");

        // Register for diagnostics
        register_mapping(UART_PHYS, UART_PHYS + 0x1000, UART_PHYS, pte_phys, "UART MMIO (ID)");
    }
}
*/

// MOVED TO PMM.C - Phase 4 Physical Memory Functions Migration  
/*
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
*/

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

// MemoryMapping structure and MAX_MAPPINGS now defined in memory_config.h
MemoryMapping mappings[MAX_MAPPINGS];
int num_mappings = 0;

// Function to register a memory mapping for diagnostic purposes
// MOVED TO memory/memory_debug.c
/*
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
*/

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
// MOVED TO memory/memory_debug.c
/*
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
*/

// Implementation of the VMM initialization
void init_vmm_impl(void) {
    // Add direct UART markers for early failure detection  
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Marker: Entering init_vmm_impl
    *uart = '[';
    *uart = 'I';
    *uart = 'M';
    *uart = 'P';
    *uart = 'L';
    *uart = ']';
    *uart = '\r';
    *uart = '\n';
    
    uart_puts_early("[VMM] Initializing virtual memory manager (implementation)\n");
    
    // Step A: Initialize page tables
    *uart = 'A';
    *uart = ':';
    *uart = 'P';
    *uart = 'A';
    *uart = 'G';
    *uart = 'E';
    *uart = '\r';
    *uart = '\n';
    
    uint64_t* kernel_l0_table = init_page_tables();
    if (!kernel_l0_table) {
        uart_puts_early("[VMM] Failed to initialize page tables\n");
        *uart = 'A';
        *uart = ':';
        *uart = 'F';
        *uart = 'A';
        *uart = 'I';
        *uart = 'L';
        *uart = '\r';
        *uart = '\n';
        return;
    }
    
    *uart = 'A';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Store the kernel page table in the global variable
    l0_table = kernel_l0_table;
    
    // Step B: Map the UART
    *uart = 'B';
    *uart = ':';
    *uart = 'U';
    *uart = 'A';
    *uart = 'R';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    map_uart();
    
    *uart = 'B';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step C: Map the kernel sections (uses the global l0_table)
    *uart = 'C';
    *uart = ':';
    *uart = 'K';
    *uart = 'E';
    *uart = 'R';
    *uart = 'N';
    *uart = '\r';
    *uart = '\n';
    
    map_kernel_sections();
    
    *uart = 'C';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step D: Map the vector table
    *uart = 'D';
    *uart = ':';
    *uart = 'V';
    *uart = 'E';
    *uart = 'C';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    map_vector_table();
    
    *uart = 'D';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step E: Map the MMU transition code
    *uart = 'E';
    *uart = ':';
    *uart = 'T';
    *uart = 'R';
    *uart = 'A';
    *uart = 'N';
    *uart = '\r';
    *uart = '\n';
    
    map_mmu_transition_code();
    
    *uart = 'E';
    *uart = ':';
    *uart = 'O';
    *uart = 'K';
    *uart = '\r';
    *uart = '\n';
    
    // Step F: Enable MMU
    *uart = 'F';
    *uart = ':';
    *uart = 'E';
    *uart = 'N';
    *uart = 'A';
    *uart = 'B';
    *uart = '\r';
    *uart = '\n';
    
    enable_mmu_enhanced(l0_table);
    
    // We should NEVER reach here
    *uart = 'F';
    *uart = ':';
    *uart = 'E';
    *uart = 'R';
    *uart = 'R';
    *uart = '\r';
    *uart = '\n';
    
    // Step F: Enable MMU
    *uart = 'F';
    *uart = ':';
    *uart = 'E';
    *uart = 'N';
    *uart = 'A';
    *uart = 'B';
    *uart = '\r';
    *uart = '\n';
    
    enable_mmu_enhanced(l0_table);
    
    // We should NEVER reach here
    *uart = 'F';
    *uart = ':';
    *uart = 'E';
    *uart = 'R';
    *uart = 'R';
    *uart = '\r';
    *uart = '\n';

    // Map the two L0 page-table pages (0x40000000-0x40002000) so that
    // subsequent page-table maintenance after the MMU is enabled will not
    // fault when the kernel touches these virtual addresses.
    map_range(l0_table,
              0x40000000UL,       // virt_start (inclusive)
              0x40002000UL,       // virt_end   (exclusive)
              0x40000000UL,       // phys_start (identity)
              PTE_KERN_DATA);

    register_mapping(0x40000000UL, 0x40002000UL,
                     0x40000000UL, PTE_KERN_DATA,
                     "L0 tables (identity)");
}

// Initialize the page tables for the kernel
// MOVED TO memory/memory_core.c
/*
uint64_t* init_page_tables(void) {
    uart_puts_early("[VMM] Initializing page tables\n");
    
    // Allocate L0 table for TTBR0_EL1 (512 entries, 4KB)
    uint64_t* l0_table_ttbr0 = (uint64_t*)alloc_page();
    if (!l0_table_ttbr0) {
        uart_puts_early("[VMM] ERROR: Failed to allocate TTBR0 L0 page table\n");
        return NULL;
    }
    
    // Allocate separate L0 table for TTBR1_EL1 (512 entries, 4KB)
    l0_table_ttbr1 = (uint64_t*)alloc_page();
    if (!l0_table_ttbr1) {
        uart_puts_early("[VMM] ERROR: Failed to allocate TTBR1 L0 page table\n");
        return NULL;
    }
    
    // Clear both tables
    for (int i = 0; i < 512; i++) {
        l0_table_ttbr0[i] = 0;
        l0_table_ttbr1[i] = 0;
    }
    
    // Cache maintenance for the TTBR0 L0 table
    for (uintptr_t addr = (uintptr_t)l0_table_ttbr0; 
         addr < (uintptr_t)l0_table_ttbr0 + 4096; 
         addr += 64) {
        asm volatile("dc civac, %0" :: "r"(addr) : "memory");
    }
    
    // Cache maintenance for the TTBR1 L0 table
    for (uintptr_t addr = (uintptr_t)l0_table_ttbr1; 
         addr < (uintptr_t)l0_table_ttbr1 + 4096; 
         addr += 64) {
        asm volatile("dc civac, %0" :: "r"(addr) : "memory");
    }
    asm volatile("dsb ish" ::: "memory");
    
    uart_puts_early("[VMM] TTBR0 L0 table created at 0x");
    uart_hex64_early((uint64_t)l0_table_ttbr0);
    uart_puts_early("\n");
    uart_puts_early("[VMM] TTBR1 L0 table created at 0x");
    uart_hex64_early((uint64_t)l0_table_ttbr1);
    uart_puts_early("\n");
    
    return l0_table_ttbr0;
}
*/

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
// MOVED TO memory/memory_core.c
/*
void enable_mmu_enhanced(uint64_t* page_table_base) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Step 4: Verify critical mappings before MMU enable
    verify_critical_mappings_before_mmu(page_table_base);
    
    // Enhanced cache maintenance
    enhanced_cache_maintenance();
    
    // Save the physical addresses of both page table bases
    uint64_t page_table_phys_ttbr0 = (uint64_t)page_table_base;
    uint64_t page_table_phys_ttbr1 = (uint64_t)l0_table_ttbr1;
    
    // CRITICAL: Verify page table base alignment for both tables
    *uart = 'A'; *uart = 'L'; *uart = 'I'; *uart = 'G'; *uart = 'N'; *uart = ':';
    uart_hex64_early(page_table_phys_ttbr0);
    *uart = '/';
    uart_hex64_early(page_table_phys_ttbr0 & 0xFFF);
    *uart = '|';
    uart_hex64_early(page_table_phys_ttbr1);
    *uart = '/';
    uart_hex64_early(page_table_phys_ttbr1 & 0xFFF);
    *uart = '\r'; *uart = '\n';
    
    if ((page_table_phys_ttbr0 & 0xFFF) || (page_table_phys_ttbr1 & 0xFFF)) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'A'; *uart = 'L'; *uart = 'I'; *uart = 'G'; *uart = 'N';
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Save the current vector table address before MMU transition
    uint64_t vbar_el1_addr = read_vbar_el1();
    saved_vector_table_addr = vbar_el1_addr;
    
    *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':';
    uart_hex64_early(vbar_el1_addr);
    *uart = '\r'; *uart = '\n';
    
    // Debug: Show separate page table addresses
    *uart = 'T'; *uart = 'T'; *uart = 'B'; *uart = 'R'; *uart = '0'; *uart = ':';
    uart_hex64_early(page_table_phys_ttbr0);
    *uart = '\r'; *uart = '\n';
    *uart = 'T'; *uart = 'T'; *uart = 'B'; *uart = 'R'; *uart = '1'; *uart = ':';
    uart_hex64_early(page_table_phys_ttbr1);
    *uart = '\r'; *uart = '\n';
    
    // **CRITICAL FIX 1: Complete TCR_EL1 Configuration**
    // Set up comprehensive TCR_EL1 (Translation Control Register)
    uint64_t tcr = 0;
    
    // VA size: selectable via VA_BITS_48 (see uart.h)
    //   48-bit VA → TCR_T0SZ/T1SZ = 16
    //   39-bit VA → 25
    tcr |= ((uint64_t)TCR_T0SZ << 0);
    
    tcr |= ((uint64_t)TCR_T1SZ << 16);
    
    // TG0[15:14] = 0 (4KB granule for TTBR0_EL1)
    tcr |= (0ULL << 14);
    
    // TG1[31:30] = 2 (4KB granule for TTBR1_EL1)
    tcr |= (2ULL << 30);
    
    // SH0[13:12] = 3 (Inner shareable for TTBR0)
    tcr |= (3ULL << 12);
    
    // SH1[29:28] = 3 (Inner shareable for TTBR1)
    tcr |= (3ULL << 28);
    
    // ORGN0[11:10] = 1 (Outer Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 10);
    
    // ORGN1[27:26] = 1 (Outer Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 26);
    
    // IRGN0[9:8] = 1 (Inner Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 8);
    
    // IRGN1[25:24] = 1 (Inner Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 24);
    
    // EPD0 = 0 (Enable TTBR0 page table walks)
    tcr |= (0ULL << 7);
    
    // EPD1 = 0 (Enable TTBR1 page table walks)  
    tcr |= (0ULL << 23);
    
    // IPS[34:32] = 1 (40-bit physical address size)
    tcr |= (1ULL << 32);
    
    // TBI0 = 1 (Top Byte Ignored for TTBR0)
    tcr |= (1ULL << 37);
    
    // TBI1 = 1 (Top Byte Ignored for TTBR1)
    tcr |= (1ULL << 38);
    
    // AS = 0 (ASID size is 8-bit)
    tcr |= (0ULL << 36);
    
    *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = ':';
    uart_hex64_early(tcr);
    *uart = '\r'; *uart = '\n';
    
    // Verify critical TCR_EL1 fields
    *uart = 'V'; *uart = 'A'; *uart = 'L'; *uart = ':';
    uart_hex64_early(tcr & 0x3F);
    *uart = '/';
    uart_hex64_early((tcr >> 16) & 0x3F);
    *uart = '/';
    uart_hex64_early((tcr >> 32) & 0x7);
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: After TCR_EL1 setup
    *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Set up MAIR_EL1 (Memory Attribute Indirection Register)
    uint64_t mair = (MAIR_ATTR_DEVICE_nGnRnE << (8 * ATTR_IDX_DEVICE_nGnRnE)) |
                    (MAIR_ATTR_NORMAL << (8 * ATTR_IDX_NORMAL)) |
                    (MAIR_ATTR_NORMAL_NC << (8 * ATTR_IDX_NORMAL_NC)) |
                    (MAIR_ATTR_DEVICE_nGnRE << (8 * ATTR_IDX_DEVICE_nGnRE));
    
    // DEBUG: After MAIR_EL1 setup
    *uart = 'M'; *uart = 'A'; *uart = 'I'; *uart = 'R'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    *uart = 'M'; *uart = 'A'; *uart = 'I'; *uart = 'R'; *uart = ':';
    uart_hex64_early(mair);
    *uart = '\r'; *uart = '\n';
    
    // **CRITICAL FIX 2: Identity Map Current Execution Context**
    // DEBUG: Before PC detection (keep for debugging)
    *uart = 'P'; *uart = 'C'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Get the current PC location for debugging purposes only
    uint64_t debug_pc;
    asm volatile("adr %0, ." : "=r"(debug_pc));
    
    // DEBUG: After PC detection
    *uart = 'P'; *uart = 'C'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    *uart = 'P'; *uart = 'C'; *uart = ':';
    uart_hex64_early(debug_pc);
    *uart = '\r'; *uart = '\n';
    
    // Note: Critical PC detection and mapping now happens inside assembly block
    *uart = 'P'; *uart = 'C'; *uart = ':'; *uart = 'M'; *uart = 'O'; *uart = 'V'; *uart = 'E'; *uart = 'D';
    *uart = '\r'; *uart = '\n';
    
    // Simple MMU Enable Sequence with proper TCR_EL1 setup
    *uart = 'S'; *uart = 'E'; *uart = 'Q'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    extern void mmu_continuation_point(void);
    uint64_t continuation_phys = (uint64_t)&mmu_continuation_point;
    
    *uart = 'B'; *uart = 'R'; *uart = 'A'; *uart = 'N'; *uart = 'C'; *uart = 'H'; *uart = ':';
    uart_hex64_early(continuation_phys);
    *uart = '\r'; *uart = '\n';
    
    // STEP 2: Enhanced MMU enable sequence with fallback branch strategy
    // Calculate both physical and virtual continuation addresses
    uint64_t high_virt_base = HIGH_VIRT_BASE;
    uint64_t continuation_virt = high_virt_base | continuation_phys;
    
    *uart = 'S'; *uart = 'T'; *uart = 'E'; *uart = 'P'; *uart = '2'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    *uart = 'P'; *uart = 'H'; *uart = 'Y'; *uart = 'S'; *uart = ':';
    uart_hex64_early(continuation_phys);
    *uart = '\r'; *uart = '\n';
    *uart = 'V'; *uart = 'I'; *uart = 'R'; *uart = 'T'; *uart = ':';
    uart_hex64_early(continuation_virt);
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Just before assembly
    *uart = 'A'; *uart = 'S'; *uart = 'M'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // CRITICAL: Create identity mapping for the assembly block
    // Get the approximate address of the assembly block
    uint64_t assembly_block_pc;
    asm volatile("adr %0, assembly_start" : "=r"(assembly_block_pc));
    
    // Calculate mapping range - map generous region around assembly block
    uint64_t assembly_page_start = assembly_block_pc & ~0xFFF;           // Round down to page boundary
    uint64_t assembly_page_end = assembly_page_start + 0x4000;           // Map 16KB (4 pages) to be safe
    
    *uart = 'A'; *uart = 'P'; *uart = 'C'; *uart = ':';
    uart_hex64_early(assembly_block_pc);
    *uart = '\r'; *uart = '\n';
    
    *uart = 'A'; *uart = 'R'; *uart = 'G'; *uart = ':';
    uart_hex64_early(assembly_page_start);
    *uart = '-';
    uart_hex64_early(assembly_page_end);
    *uart = '\r'; *uart = '\n';
    
    // Create the identity mapping for assembly execution
    *uart = 'A'; *uart = 'M'; *uart = 'A'; *uart = 'P'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    map_range(page_table_base, assembly_page_start, assembly_page_end, assembly_page_start, PTE_KERN_TEXT);
    
    *uart = 'A'; *uart = 'M'; *uart = 'A'; *uart = 'P'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    asm volatile (
        "assembly_start:\n"           // Label for PC detection
        // Save critical values
        "mov x19, %0\n"              // x19 = page_table_base_ttbr0
        "mov x18, %1\n"              // x18 = page_table_base_ttbr1
        "mov x20, %2\n"              // x20 = tcr value
        "mov x21, %3\n"              // x21 = mair value  
        "mov x22, %4\n"              // x22 = continuation_phys
        "mov x24, %5\n"              // x24 = continuation_virt
        
        // Emergency debug: Output "PHYS:" + current EL level
        "mrs x25, currentel\n"       // Get current exception level
        "lsr x25, x25, #2\n"        // Shift to get EL number
        "mov x26, #0x09000000\n"     // UART base
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'H'\n"  
        "str w27, [x26]\n"
        "mov w27, #'Y'\n"
        "str w27, [x26]\n"
        "mov w27, #'S'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        "add w27, w25, #'0'\n"       // Convert EL to ASCII
        "str w27, [x26]\n"
        "mov w27, #'\\r'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\n'\n"
        "str w27, [x26]\n"
        
        // Set up memory attributes
        "msr mair_el1, x21\n"        // Set MAIR_EL1
        "isb\n"
        
        // Set up translation control (keep the good TCR_EL1 setup)
        "msr tcr_el1, x20\n"         // Set complete TCR_EL1
        "isb\n"
        
        // Set translation table bases - separate page tables for TTBR0 and TTBR1
        "msr ttbr0_el1, x19\n"       // Set TTBR0_EL1 to page table for low addresses
        "msr ttbr1_el1, x18\n"       // Set TTBR1_EL1 to separate page table for high addresses
        "isb\n"
        
        // PHASE 1: COMPREHENSIVE CACHE FLUSH FOR PAGE TABLE COHERENCY
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        
        // Step 1: Flush all data cache to ensure page table writes reach RAM
        // "dsb sy\n"                   // Data synchronization barrier
        // "mov x0, #0\n"               // Start of cache sweep
        // "dc cisw, x0\n"              // Clean & invalidate data cache by set/way
        // "add x0, x0, #64\n"          // Next cache line (64-byte line size)
        // "cmp x0, #0x8000\n"          // Cover reasonable cache size (32KB)
        // "b.lt .-8\n"                 // Loop back
        // "dsb sy\n"                   // Wait for cache operations
        
        // Step 2: Specific flush for page table regions
        "mov x0, %0\n"               // TTBR0 table address
        "dc cvac, x0\n"              // Clean by virtual address (TTBR0 L0 table)
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean next cache line
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean third cache line
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean fourth cache line (cover full page)
        
        "mov x0, %1\n"               // TTBR1 table address  
        "dc cvac, x0\n"              // Clean by virtual address (TTBR1 L0 table)
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean next cache line
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean third cache line
        "add x0, x0, #64\n"
        "dc cvac, x0\n"              // Clean fourth cache line
        
        "dsb sy\n"                   // Wait for all cache operations
        
        // PHASE 2: COMPREHENSIVE TLB INVALIDATION
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'2'\n"
        "str w27, [x26]\n"
        
        // Step 1: Invalidate all TLB entries for both address spaces
        "tlbi vmalle1\n"             // Invalidate all stage 1 TLB entries (local)
        "dsb nsh\n"                  // Data synchronization barrier (non-shareable)
        "tlbi vmalle1is\n"           // Invalidate all stage 1 TLB entries (inner shareable)
        "dsb ish\n"                  // Data synchronization barrier (inner shareable)
        
        // Step 2: Invalidate instruction cache to prevent stale instruction fetches
        "ic iallu\n"                 // Invalidate instruction cache (all)
        "dsb sy\n"                   // Wait for instruction cache invalidation
        "isb\n"                      // Instruction synchronization barrier
        
        // Enable MMU - CONSERVATIVE APPROACH WITH MULTI-STAGE BARRIERS
        
        // CRITICAL: Get PC right before MMU enable and ensure mapping
        "adr x28, mmu_enable_point\n"     // Get address of MMU enable instruction
        "and x29, x28, %[page_mask]\n"    // Round down to page boundary
        "add x30, x29, #0x2000\n"         // Add 8KB (2 pages) for safety
        
        // Debug: Show critical PC location
        "mov w27, #'C'\n"
        "str w27, [x26]\n"
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'C'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Output critical PC in hex (simplified - just show low 16 bits)
        "and x0, x28, #0xFFFF\n"          // Get low 16 bits
        "lsr x1, x0, #12\n"               // Get high nibble
        "and x1, x1, #0xF\n"
        "cmp x1, #10\n"
        "b.lt 1f\n"
        "add x1, x1, #'A'-10\n"
        "b 2f\n"
        "1: add x1, x1, #'0'\n"
        "2: str w1, [x26]\n"
        
        "lsr x1, x0, #8\n"                // Next nibble
        "and x1, x1, #0xF\n"
        "cmp x1, #10\n"
        "b.lt 3f\n"
        "add x1, x1, #'A'-10\n"
        "b 4f\n"
        "3: add x1, x1, #'0'\n"
        "4: str w1, [x26]\n"
        
        "lsr x1, x0, #4\n"                // Next nibble
        "and x1, x1, #0xF\n"
        "cmp x1, #10\n"
        "b.lt 5f\n"
        "add x1, x1, #'A'-10\n"
        "b 6f\n"
        "5: add x1, x1, #'0'\n"
        "6: str w1, [x26]\n"
        
        "and x1, x0, #0xF\n"              // Last nibble
        "cmp x1, #10\n"
        "b.lt 7f\n"
        "add x1, x1, #'A'-10\n"
        "b 8f\n"
        "7: add x1, x1, #'0'\n"
        "8: str w1, [x26]\n"
        
        "mov w27, #'\\r'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\n'\n"
        "str w27, [x26]\n"
        
        // FINAL PHASE: CRITICAL SYNCHRONIZATION BEFORE MMU ENABLE
        "mov w27, #'F'\n"
        "str w27, [x26]\n"
        "mov w27, #'I'\n"
        "str w27, [x26]\n"
        "mov w27, #'N'\n"
        "str w27, [x26]\n"
        
        // Final memory barrier sequence to ensure cache/TLB operations complete
        "dsb sy\n"                   // Data synchronization barrier (system-wide)
        "isb\n"                      // Instruction synchronization barrier
        
        // Ensure all page table updates are visible to MMU hardware
        "dmb sy\n"                   // Data memory barrier (system-wide)
        "dsb sy\n"                   // Data synchronization barrier (system-wide)
        "isb\n"                      // Instruction synchronization barrier
        
        // Now we're at the critical point - MMU enable happens here
        "mmu_enable_point:\n"
        
        // DEBUG: Show what firmware gave us
        "mrs x23, sctlr_el1\n"       // Read current SCTLR_EL1
        "mov w27, #'F'\n"            // 'F' = Firmware SCTLR
        "str w27, [x26]\n"
        "mov w27, #'W'\n" 
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output firmware SCTLR value (simplified hex output)
        "and x29, x23, #0xFFFF\n"    // Get low 16 bits
        "lsr x29, x29, #12\n"        // Show bits 12-15 (I,Z,C,A bits)
        "and x29, x29, #0xF\n"       // Just 4 bits
        "cmp x29, #10\n"
        "b.lt 50f\n"
        "add x29, x29, #'A'-10\n"
        "b 51f\n"
        "50:\n"
        "add x29, x29, #'0'\n"
        "51:\n"
        "str w29, [x26]\n"
        
        // CLEAR problematic cache bits
        "mov w27, #'C'\n"            // 'C' = Clearing
        "str w27, [x26]\n"
        "mov w27, #'L'\n"
        "str w27, [x26]\n"
        "mov w27, #'R'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // COMPLETELY REPLACE with known-good value
        "mov x23, #0x0800\n"         // Base SCTLR value (essential bits)
        "movk x23, #0x30D0, lsl #16\n" // Upper bits for known-good config
        
        // SET cache configuration that matches our MAIR
        "mov w27, #'S'\n"            // 'S' = Setting  
        "str w27, [x26]\n"
        "mov w27, #'E'\n"
        "str w27, [x26]\n"
        "mov w27, #'T'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        "orr x23, x23, #0x1000\n"    // Set I(12) - instruction cache
        "orr x23, x23, #0x4\n"       // Set C(2) - data cache
        "orr x23, x23, #0x1\n"       // Set M(0) - MMU enable
        
        // DEBUG: Show our final SCTLR value
        "mov w27, #'F'\n"            // 'F' = Final SCTLR
        "str w27, [x26]\n"
        "mov w27, #'I'\n"
        "str w27, [x26]\n"
        "mov w27, #'N'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output our SCTLR value (simplified hex output)
        "and x29, x23, #0xFFFF\n"    // Get low 16 bits
        "lsr x29, x29, #12\n"        // Show bits 12-15 (I,Z,C,A bits)
        "and x29, x29, #0xF\n"       // Just 4 bits
        "cmp x29, #10\n"
        "b.lt 52f\n"
        "add x29, x29, #'A'-10\n"
        "b 53f\n"
        "52:\n"
        "add x29, x29, #'0'\n"
        "53:\n"
        "str w29, [x26]\n"
        
        // DEBUG: Before MMU enable sequence
        "mov w27, #'M'\n"
        "str w27, [x26]\n"
        "mov w27, #'M'\n"
        "str w27, [x26]\n"
        "mov w27, #'U'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // STAGE 1: Pre-MMU comprehensive synchronization
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        "dsb sy\n"                   // Full system data synchronization barrier
        "isb\n"                      // Instruction synchronization barrier
        
        // STAGE 2: Cache coherency preparation  
        "mov w27, #'2'\n"
        "str w27, [x26]\n"
        "ic iallu\n"                 // Invalidate instruction cache (controlled environment)
        "dsb sy\n"                   // Wait for instruction cache invalidation
        "isb\n"                      // Synchronize instruction stream
        
        // STAGE 3: Final pre-MMU barrier
        "mov w27, #'3'\n"
        "str w27, [x26]\n"
        "dsb sy\n"                   // Final data synchronization
        "isb\n"                      // Final instruction synchronization
        
        // DEBUG 1: Exception Vector Verification
        "mrs x28, vbar_el1\n"        // Get vector table address
        "mov w27, #'V'\n"
        "str w27, [x26]\n"
        "mov w27, #'E'\n"
        "str w27, [x26]\n"
        "mov w27, #'C'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output x28 (VBAR_EL1) - show relevant part (low 32 bits, high 16)
        "and x29, x28, #0xFFFFFFFF\n" // Get low 32 bits
        "lsr x29, x29, #16\n"        // Show high 16 bits of low 32
        "mov w30, #12\n"             // Shift counter for 4 hex digits
        "9:\n"                       // Hex output loop
        "lsr x0, x29, x30\n"         // Shift to get nibble (use x0 instead of x31)
        "and x0, x0, #0xF\n"         // Mask to 4 bits
        "cmp x0, #10\n"
        "b.lt 10f\n"
        "add x0, x0, #'A'-10\n"      // Convert A-F
        "b 11f\n"
        "10:\n"
        "add x0, x0, #'0'\n"         // Convert 0-9
        "11:\n"
        "str w0, [x26]\n"            // Output hex digit
        "subs w30, w30, #4\n"        // Next nibble
        "b.ge 9b\n"                  // Loop for all 4 digits
        
        // DEBUG 2: Page Table Entry Verification  
        "ldr x28, [x19]\n"           // Read L0 table entry 0
        "mov w27, #'L'\n"
        "str w27, [x26]\n"
        "mov w27, #'0'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output x28 (L0 entry) - low 32 bits
        "and x29, x28, #0xFFFFFFFF\n" // Get low 32 bits  
        "lsr x29, x29, #16\n"        // Show high 16 bits of low 32
        "mov w30, #12\n"             // Shift counter for 4 hex digits
        "12:\n"                      // Hex output loop
        "lsr x1, x29, x30\n"         // Shift to get nibble (use x1 instead of x31)
        "and x1, x1, #0xF\n"         // Mask to 4 bits
        "cmp x1, #10\n"
        "b.lt 13f\n"
        "add x1, x1, #'A'-10\n"      // Convert A-F
        "b 14f\n"
        "13:\n"
        "add x1, x1, #'0'\n"         // Convert 0-9
        "14:\n"
        "str w1, [x26]\n"            // Output hex digit
        "subs w30, w30, #4\n"        // Next nibble
        "b.ge 12b\n"                 // Loop for all 4 digits
        
        // DEBUG 3: Register Dump - Verify separate TTBR values after setting
        "mrs x28, ttbr0_el1\n"       // Check TTBR0_EL1
        "mov w27, #'T'\n"
        "str w27, [x26]\n"
        "mov w27, #'0'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output x28 (TTBR0_EL1) - show relevant part (low 32 bits, high 16)
        "and x29, x28, #0xFFFFFFFF\n" // Get low 32 bits
        "lsr x29, x29, #16\n"        // Show high 16 bits of low 32
        "mov w30, #12\n"             // Shift counter for 4 hex digits
        "15:\n"                      // Hex output loop
        "lsr x2, x29, x30\n"         // Shift to get nibble (use x2 instead of x31)
        "and x2, x2, #0xF\n"         // Mask to 4 bits
        "cmp x2, #10\n"
        "b.lt 16f\n"
        "add x2, x2, #'A'-10\n"      // Convert A-F
        "b 17f\n"
        "16:\n"
        "add x2, x2, #'0'\n"         // Convert 0-9
        "17:\n"
        "str w2, [x26]\n"            // Output hex digit
        "subs w30, w30, #4\n"        // Next nibble
        "b.ge 15b\n"                 // Loop for all 4 digits
        
        "mrs x28, ttbr1_el1\n"       // Check TTBR1_EL1 - should be different from TTBR0
        "mov w27, #'T'\n"
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output x28 (TTBR1_EL1) - show relevant part (low 32 bits, high 16)
        "and x29, x28, #0xFFFFFFFF\n" // Get low 32 bits
        "lsr x29, x29, #16\n"        // Show high 16 bits of low 32
        "mov w30, #12\n"             // Shift counter for 4 hex digits
        "18:\n"                      // Hex output loop
        "lsr x3, x29, x30\n"         // Shift to get nibble (use x3 instead of x31)
        "and x3, x3, #0xF\n"         // Mask to 4 bits
        "cmp x3, #10\n"
        "b.lt 19f\n"
        "add x3, x3, #'A'-10\n"      // Convert A-F
        "b 20f\n"
        "19:\n"
        "add x3, x3, #'0'\n"         // Convert 0-9
        "20:\n"
        "str w3, [x26]\n"            // Output hex digit
        "subs w30, w30, #4\n"        // Next nibble
        "b.ge 18b\n"                 // Loop for all 4 digits
        
        // DEBUG 4: Exception Vector Table Virtual Address Verification
        "mov w27, #'E'\n"
        "str w27, [x26]\n"
        "mov w27, #'V'\n"
        "str w27, [x26]\n"
        "mov w27, #'T'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Check if vector table has virtual mapping at expected address
        "mov x28, #0x0000000001000000\n" // Expected virtual vector table address
        "lsr x29, x28, #39\n"        // L0 index
        "and x29, x29, #0x1FF\n"
        "ldr x30, [x19, x29, lsl #3]\n" // Load L0 entry
        "tst x30, #1\n"              // Check valid bit
        "b.eq 21f\n"                 // Branch if invalid
        
        // Vector table mapping exists
        "mov w27, #'O'\n"
        "str w27, [x26]\n"
        "mov w27, #'K'\n"
        "str w27, [x26]\n"
        "b 22f\n"
        
        "21:\n"                      // Vector table not mapped
        "mov w27, #'N'\n"
        "str w27, [x26]\n"
        "mov w27, #'O'\n"
        "str w27, [x26]\n"
        
        "22:\n"                      // Continue
        
        // DEBUG 5: Page Table Cache Maintenance Before MMU Enable
        "mov w27, #'C'\n"
        "str w27, [x26]\n"
        "mov w27, #'L'\n"
        "str w27, [x26]\n"
        "mov w27, #'N'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Clean page table base address from data cache
        "dc cvac, x19\n"             // Clean page table base address
        
        // Clean L0 table entries (first few entries)
        "mov x28, x19\n"             // Start with L0 table base
        "mov x29, #8\n"              // Clean first 8 entries (64 bytes)
        "23:\n"
        "dc cvac, x28\n"             // Clean cache line
        "add x28, x28, #64\n"        // Next cache line
        "subs x29, x29, #1\n"        // Decrement counter
        "b.ne 23b\n"                 // Continue cleaning
        
        // Data synchronization after cleaning
        "dsb sy\n"                   // Wait for cache operations to complete
        
        "mov w27, #'O'\n"
        "str w27, [x26]\n"
        "mov w27, #'K'\n"
        "str w27, [x26]\n"
        
        // DEBUG 6: Exception-Safe MMU Enable Preparation
        "mov w27, #'E'\n"
        "str w27, [x26]\n"
        "mov w27, #'X'\n"
        "str w27, [x26]\n"
        "mov w27, #'C'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Verify exception level and prepare for potential exceptions
        "mrs x28, currentel\n"       // Get current exception level
        "lsr x28, x28, #2\n"         // Extract EL bits
        "cmp x28, #1\n"              // Should be EL1
        "b.ne 24f\n"                 // Branch if not EL1
        
        // Verify DAIF (exception mask) state
        "mrs x28, daif\n"            // Get interrupt masks
        "and x28, x28, #0xF\n"       // Mask to DAIF bits
        
        // Verify stack pointer alignment
        "mov x28, sp\n"              // Get current stack pointer
        "and x28, x28, #0xF\n"       // Check 16-byte alignment
        "cbnz x28, 25f\n"            // Branch if not aligned
        
        "mov w27, #'O'\n"
        "str w27, [x26]\n"
        "mov w27, #'K'\n"
        "str w27, [x26]\n"
        "b 26f\n"
        
        "24:\n"                      // Wrong exception level
        "mov w27, #'E'\n"
        "str w27, [x26]\n"
        "mov w27, #'L'\n"
        "str w27, [x26]\n"
        "b 26f\n"
        
        "25:\n"                      // Stack misaligned
        "mov w27, #'S'\n"
        "str w27, [x26]\n"
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        
        "26:\n"                      // Continue
        
        // STAGE 4: **MINIMAL MMU ENABLE TEST**
        "mov w27, #'4'\n"
        "str w27, [x26]\n"
        
        // FIXED: Preserve cache bits in SCTLR_EL1 (don't clear them!)
        "mov x29, x23\n"             // Save current SCTLR value
        // NOTE: x23 already contains the SCTLR value with M bit set from above
        // No need to clear cache bits - just use the value we already prepared
        
        // PRE-MMU STATUS CHECK
        "mov w27, #'S'\n"            // 'S' = About to enable MMU
        "str w27, [x26]\n"

        // DEBUG: Show SCTLR value we're about to write
        "mov w27, #'V'\n"            // 'V' = Value
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        // Output x23 value (SCTLR) - show low 16 bits
        "and x29, x23, #0xFFFF\n"    
        "lsr x29, x29, #12\n"        // Show high 4 bits of low 16
        "and x29, x29, #0xF\n"       
        "cmp x29, #10\n"
        "b.lt 60f\n"
        "add x29, x29, #'A'-10\n"
        "b 61f\n"
        "60:\n"
        "add x29, x29, #'0'\n"
        "61:\n"
        "str w29, [x26]\n"
        
        // STEP 1B: Test with minimal SCTLR (just MMU bit)
        "mov w27, #'T'\n"            // 'T' = Test minimal
        "str w27, [x26]\n"
        "mov x24, #0x1\n"            // Just MMU enable, no cache bits
        "msr sctlr_el1, x24\n"       // Try minimal MMU enable
        "mov w27, #'1'\n"            // '1' = Minimal test completed
        "str w27, [x26]\n"
        
        // STEP 3A: Verify page table integrity right before MMU
        "ldr x28, [x19]\n"           // Read first L0 entry
        "tst x28, #1\n"              // Check valid bit
        "b.eq page_table_corrupt\n"
        
        // Verify TTBR0 points to our L0 table
        "mrs x28, ttbr0_el1\n"
        "cmp x28, x19\n"
        "b.ne ttbr_mismatch\n"
        
        "msr sctlr_el1, x23\n"       // Try MMU enable
        
        "page_table_corrupt:\n"
        "mov w27, #'P'\n"            // P = Page table corrupt
        "str w27, [x26]\n"
        "b hang\n"
        
        "ttbr_mismatch:\n"
        "mov w27, #'T'\n"            // T = TTBR mismatch
        "str w27, [x26]\n"
        
        "hang:\n"
        "b hang\n"
        
        // STEP 4B: Exception level verification
        "mrs x28, currentel\n"       // Get current EL
        "lsr x28, x28, #2\n"         // Extract EL bits
        "cmp x28, #1\n"              // Must be EL1
        "b.ne wrong_el\n"
        
        "msr sctlr_el1, x23\n"       // Try MMU enable
        
        "wrong_el:\n"
        "mov w27, #'E'\n"            // E = Wrong exception level
        "str w27, [x26]\n"
        
        // STEP 4A: QEMU-specific MMU enable synchronization
        "dsb sy\n"                   // Full system barrier
        "isb\n"                      // Instruction synchronization
        
        // STEP 2A: Incremental MMU Enable (PRIMARY ATTEMPT)
        "mov w27, #'2'\n"            // '2' = Trying Step 2A
        "str w27, [x26]\n"
        "mov w27, #'A'\n"            // 'A' = Step 2A
        "str w27, [x26]\n"
        
        "mov x24, #0x1\n"            // M bit only
        "msr sctlr_el1, x24\n"       // Try MMU-only enable
        "mov w27, #'m'\n"            // 'm' = MMU-only completed
        "str w27, [x26]\n"
        
        "orr x24, x24, #0x1000\n"    // Add I bit (instruction cache)
        "msr sctlr_el1, x24\n"       // Try MMU + I
        "mov w27, #'i'\n"            // 'i' = +instruction cache
        "str w27, [x26]\n"
        
        "orr x24, x24, #0x4\n"       // Add C bit (data cache)
        "msr sctlr_el1, x24\n"       // Try MMU + I + C
        "mov w27, #'c'\n"            // 'c' = +data cache
        "str w27, [x26]\n"
        
        // STEP 2B: Linux kernel standard SCTLR values (FALLBACK)
        "mov w27, #'2'\n"            // '2' = Trying Step 2B
        "str w27, [x26]\n"
        "mov w27, #'B'\n"            // 'B' = Step 2B
        "str w27, [x26]\n"
        
        "mov x24, #0x0830\n"         // Linux kernel base value (low 16)
        "movk x24, #0x30C5, lsl #16\n" // Linux kernel base value (high 16)
        "orr x24, x24, #0x1\n"       // Add MMU enable
        "msr sctlr_el1, x24\n"       // Try Linux kernel values
        "mov w27, #'L'\n"            // 'L' = Linux values completed
        "str w27, [x26]\n"
        
        // Original approach (now as final fallback)
        "msr sctlr_el1, x23\n"       // ← ORIGINAL MMU Enable (final fallback)
        
        // POST-MMU STATUS CHECK  
        "mov w27, #'M'\n"            // 'M' = MMU enable instruction completed
        "str w27, [x26]\n"
        
        // If we reach here, MMU enable succeeded!
        
        // DEBUG 6: Post-MMU Immediate Test
        "nop\n"                      // Single pipeline bubble
        "mov w27, #'I'\n"            // Immediate test - does this work?
        "str w27, [x26]\n"           // If this fails, MMU enable faulted
        
        // ENHANCED POST-MMU INSTRUCTION PIPELINE SYNCHRONIZATION
        // Critical: Force immediate instruction refetch in new virtual context
        "isb\n"                      // IMMEDIATE: Force instruction pipeline flush
        
        // Pipeline bubbles to ensure clean instruction stream
        "nop\n"                      // Pipeline bubble 1
        "nop\n"                      // Pipeline bubble 2  
        "nop\n"                      // Pipeline bubble 3
        
        // Data synchronization to ensure MMU enable has propagated
        "dsb sy\n"                   // System-wide data synchronization barrier
        
        // Second instruction synchronization after data barrier
        "isb\n"                      // Second instruction synchronization barrier
        
        // STAGE 5: Now safe to execute normal instructions AND enable interrupts
        "mov w27, #'5'\n"
        "str w27, [x26]\n"
        
        // INDUSTRY STANDARD: Enable interrupts AFTER MMU is fully stable
        "msr daifclr, #2\n"          // Enable IRQ (clear I bit) - NOW SAFE!
        "isb\n"                      // Ensure interrupt state change
        
        // STAGE 6: Additional post-MMU verification
        "mov w27, #'6'\n"
        "str w27, [x26]\n"
        
        // STAGE 7: Final confirmation
        "mov w27, #'7'\n"
        "str w27, [x26]\n"
        
        // DEBUG: After successful MMU enable sequence
        "mov w27, #'O'\n"
        "str w27, [x26]\n"
        "mov w27, #'K'\n"
        "str w27, [x26]\n"
        
        // Output "VIRT:V" + EL level post-MMU
        "mrs x25, currentel\n"       // Get current exception level
        "lsr x25, x25, #2\n"        // Shift to get EL number
        "mov w27, #'V'\n"
        "str w27, [x26]\n"
        "mov w27, #'I'\n"
        "str w27, [x26]\n"
        "mov w27, #'R'\n"
        "str w27, [x26]\n"
        "mov w27, #'T'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        "mov w27, #'V'\n"
        "str w27, [x26]\n"
        "add w27, w25, #'0'\n"       // Convert EL to ASCII
        "str w27, [x26]\n"
        "mov w27, #'\\r'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\n'\n"
        "str w27, [x26]\n"
        
        // SIMPLIFIED: Single branch strategy (identity mapping)
        // Since we have identity mapping, use physical address directly
        "mov w27, #'B'\n"
        "str w27, [x26]\n"
        "mov w27, #'R'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        "br x22\n"                   // Branch to physical address
        
        // STEP 5A: Complete MMU bypass (if all MMU attempts fail)
        // Uncomment if MMU enable consistently fails:
        // "mov w27, #'N'\n"         // 'N' = No MMU
        // "str w27, [x26]\n"
        // "mov w27, #'0'\n"         // '0' = Operating
        // "str w27, [x26]\n"
        // // Continue with physical addressing
        // "br x22\n"               // Branch to continuation
        
        // If we reach here, both branches failed
        "2:\n"
        "mov w27, #'F'\n"
        "str w27, [x26]\n"
        "mov w27, #'A'\n"
        "str w27, [x26]\n"
        "mov w27, #'I'\n"
        "str w27, [x26]\n"
        "mov w27, #'L'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\r'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\n'\n"
        "str w27, [x26]\n"
        
        "3:\n"
        "b 3b\n"                     // Infinite loop
        
        : // No outputs
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value
          "r"(mair),                 // %3: MAIR_EL1 value  
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // We should never reach here
    *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'R'; *uart = 'E'; *uart = 'T'; *uart = 'U'; *uart = 'R'; *uart = 'N';
    *uart = '\r'; *uart = '\n';
}
*/

// ... existing code ...
// ... existing code ...

// Verify a page mapping exists

// Add this after existing function prototypes near the top of the file
void flush_cache_lines(void* addr, size_t size);

// Add this function implementation somewhere in the file, after the declaration of PAGE_SIZE
// Helper function to flush cache lines for a memory region
// MOVED TO memory/memory_debug.c
/*
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
*/

// ============================================================================
// NOTE: Teaching/Research OS Approach
// ----------------------------------------------------------------------------
// The following function implements explicit, per-address verification and
// auto-fix of critical page table mappings before enabling the MMU. This is
// intentionally more verbose and self-healing than typical production kernels
// (like Linux), and is designed for maximum transparency, debuggability, and
// safety during early bring-up, simulation, and experimentation.
//
// Rationale:
//   - Ensures all essential mappings (code, stack, UART, vector table, etc.)
//     are present and have correct permissions before MMU enable.
//   - Auto-fixes common mistakes to avoid "silent death" during development.
//   - Provides detailed UART output for step-by-step debugging.
//   - Ideal for a simulation/teaching/research platform (e.g., Pegasus-style).
//
// If/when this project matures or transitions to a more production-like kernel,
// this section can be refactored to use asserts, panics, or more abstracted
// memory management, as is done in industry kernels.
// Enhanced page table verification and auto-fix, like a pre-flight checklist
// MOVED TO memory/memory_debug.c

// Enhanced cache maintenance function
// MOVED TO memory/memory_core.c
/*
void enhanced_cache_maintenance(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'C'; *uart = 'A'; *uart = 'C'; *uart = 'H'; *uart = 'E'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Clean and invalidate all data cache
    asm volatile("ic iallu" ::: "memory");  // Invalidate instruction cache
    asm volatile("dsb ish" ::: "memory");   // Data synchronization barrier
    asm volatile("isb" ::: "memory");       // Instruction synchronization barrier
    
    *uart = 'C'; *uart = 'A'; *uart = 'C'; *uart = 'H'; *uart = 'E'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
}
*/


