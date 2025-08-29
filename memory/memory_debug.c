#include "memory_debug.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"
#include "../include/memory_config.h"

// External variables and functions needed by debug functions
extern MemoryMapping mappings[MAX_MAPPINGS];
extern int num_mappings;
extern bool debug_vmm;
extern uint64_t* l0_table;
extern uint64_t* l0_table_ttbr1;
extern uint64_t saved_vector_table_addr;

// External function declarations
extern uint64_t* get_kernel_page_table(void);
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
extern uint64_t get_pte(uint64_t virt_addr);
extern uint64_t read_vbar_el1(void);
extern void uart_puts_early(const char* str);
extern void uart_hex64_early(uint64_t val);

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
void verify_critical_mappings_before_mmu(uint64_t* page_table_base) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'V'; *uart = 'E'; *uart = 'R'; *uart = 'I'; *uart = 'F'; *uart = 'Y'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    if (!page_table_base) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'N'; *uart = 'U'; *uart = 'L'; *uart = 'L';
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Critical addresses to verify
    extern void mmu_continuation_point(void);
    uint64_t continuation_addr = (uint64_t)&mmu_continuation_point;
    uint64_t uart_phys = 0x09000000;
    uint64_t uart_virt = UART_VIRT;
    uint64_t vector_table_addr = read_vbar_el1();
    
    uint64_t high_virt_base = HIGH_VIRT_BASE;
    uint64_t continuation_virt = high_virt_base | continuation_addr;
    
    *uart = 'A'; *uart = 'D'; *uart = 'D'; *uart = 'R'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    *uart = 'C'; *uart = 'O'; *uart = 'N'; *uart = 'T'; *uart = ':';
    uart_hex64_early(continuation_addr);
    *uart = '\r'; *uart = '\n';
    *uart = 'V'; *uart = 'I'; *uart = 'R'; *uart = 'T'; *uart = ':';
    uart_hex64_early(continuation_virt);
    *uart = '\r'; *uart = '\n';
    *uart = 'U'; *uart = 'P'; *uart = 'H'; *uart = 'Y'; *uart = 'S'; *uart = ':';
    uart_hex64_early(uart_phys);
    *uart = '\r'; *uart = '\n';
    *uart = 'U'; *uart = 'V'; *uart = 'I'; *uart = 'R'; *uart = 'T'; *uart = ':';
    uart_hex64_early(uart_virt);
    *uart = '\r'; *uart = '\n';
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = 'T'; *uart = ':';
    uart_hex64_early(vector_table_addr);
    *uart = '\r'; *uart = '\n';
    
    // Array of critical addresses to verify and auto-fix
    struct {
        uint64_t addr;
        const char* name;
        bool should_be_executable;
    } critical_mappings[] = {
        {continuation_addr, "Continuation (phys)", true},
        {continuation_virt, "Continuation (virt)", true},
        {uart_phys, "UART (phys)", false},
        {uart_virt, "UART (virt)", false},
        {vector_table_addr, "Vector table", true},
        {0, NULL, false} // End marker
    };
    
    *uart = 'V'; *uart = 'E'; *uart = 'R'; *uart = 'I'; *uart = 'F'; *uart = 'Y'; *uart = ':'; *uart = 'L'; *uart = 'O'; *uart = 'O'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
    
    for (int i = 0; critical_mappings[i].addr != 0; i++) {
        uint64_t addr = critical_mappings[i].addr;
        const char* name = critical_mappings[i].name;
        bool should_be_exec = critical_mappings[i].should_be_executable;
        
        *uart = 'I'; *uart = '0' + i; *uart = ':';
        
        // Select the appropriate root page-table: TTBR1 is used for
        // high kernel virtual addresses (>= HIGH_VIRT_BASE), TTBR0 for
        // everything else.  This mirrors the logic in map_range().

        uint64_t* root_l0 = (addr >= HIGH_VIRT_BASE) ? l0_table_ttbr1
                                                      : page_table_base;

        // Get L3 table for this address using the chosen root table
        uint64_t* l3_table = get_l3_table_for_addr(root_l0, addr);
        if (!l3_table) {
            *uart = 'N'; *uart = 'O'; *uart = 'L'; *uart = '3';
            *uart = '\r'; *uart = '\n';
            continue;
        }
        
        // Calculate L3 index
        uint64_t l3_idx = (addr >> 12) & 0x1FF;
        uint64_t pte = l3_table[l3_idx];
        
        if (!(pte & PTE_VALID)) {
            *uart = 'N'; *uart = 'O'; *uart = 'M'; *uart = 'A'; *uart = 'P';
            *uart = '\r'; *uart = '\n';
            // NEW: treat missing critical mapping as fatal – stay here so
            // the developer sees the problem instantly.
            while (1) { /* PANIC: critical mapping missing */ }
        }
        
        // Check and fix executable permissions
        bool is_executable = !(pte & PTE_PXN);
        bool needs_fix = (should_be_exec && !is_executable) || (!should_be_exec && is_executable);
        
        if (needs_fix) {
            *uart = 'F'; *uart = 'I'; *uart = 'X';
            
            uint64_t new_pte = pte;
            if (should_be_exec) {
                // Clear PXN bit to make executable
                new_pte &= ~PTE_PXN;
            } else {
                // Set PXN bit to make non-executable
                new_pte |= PTE_PXN;
            }
            
            // Update PTE with proper cache maintenance
            asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
            asm volatile("dsb ish" ::: "memory");
            
            l3_table[l3_idx] = new_pte;
            
            asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
            asm volatile("dsb ish" ::: "memory");
            
            // Invalidate TLB for this specific address
            asm volatile("tlbi vaae1is, %0" :: "r"(addr >> 12) : "memory");
            asm volatile("dsb ish" ::: "memory");
            
            *uart = '>';
        }
        
        // Report final status
        is_executable = !(l3_table[l3_idx] & PTE_PXN);
        if (should_be_exec && is_executable) {
            *uart = 'E'; *uart = 'X'; *uart = 'E'; *uart = 'C';
        } else if (!should_be_exec && !is_executable) {
            *uart = 'N'; *uart = 'O'; *uart = 'E'; *uart = 'X'; *uart = 'E'; *uart = 'C';
        } else {
            *uart = 'W'; *uart = 'R'; *uart = 'O'; *uart = 'N'; *uart = 'G';
        }
        *uart = '\r'; *uart = '\n';
    }
    
    /* MOVED TO mmu_policy.c - mmu_comprehensive_tlbi_sequence()
    // A new, conservative TLB invalidation - step by step, local operations only
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = ':'; *uart = 'C'; *uart = 'O'; *uart = 'N'; *uart = 'S'; *uart = 'E'; *uart = 'R'; *uart = 'V';
    *uart = '\r'; *uart = '\n';
    
    // Step 1: Basic data synchronization
    *uart = 'S'; *uart = '1'; *uart = ':'; *uart = 'D'; *uart = 'S'; *uart = 'B';
    asm volatile("dsb sy" ::: "memory");  // System-wide, but simples
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    // Step 2: Local TLB invalidation (no inner-shareable domain)
    *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = 'L';
    asm volatile("tlbi vmalle1" ::: "memory");  // Local core only, no "is" suffix
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    // Step 3: Wait for TLB operation completion
    *uart = 'S'; *uart = '3'; *uart = ':'; *uart = 'W'; *uart = 'A'; *uart = 'I'; *uart = 'T';
    asm volatile("dsb nsh" ::: "memory");  // Non-shareable domain only
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    // Step 4: Minimal instruction cache invalidation (skip for now)
    *uart = 'S'; *uart = '4'; *uart = ':'; *uart = 'S'; *uart = 'K'; *uart = 'I'; *uart = 'P'; *uart = 'I'; *uart = 'C';
    // asm volatile("ic iallu" ::: "memory");  // COMMENTED OUT - often problematic
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    // Step 5: Final barrier
    *uart = 'S'; *uart = '5'; *uart = ':'; *uart = 'I'; *uart = 'S'; *uart = 'B';
    asm volatile("isb" ::: "memory");
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
    
    *uart = 'V'; *uart = 'E'; *uart = 'R'; *uart = 'I'; *uart = 'F'; *uart = 'Y'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    */
} 