#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"
#include "../include/memory_config.h"
#include "../include/memory_core.h"
#include "../include/mmu_policy.h"  // MMU Policy layer for conservative testing

// External global variables from vmm.c
extern uint64_t* l0_table;
extern uint64_t* l0_table_ttbr1;
extern uint64_t saved_vector_table_addr;

// Forward declarations for functions used by enable_mmu_enhanced()
extern void mmu_trampoline_continuation_point(void);
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
extern void verify_critical_mappings_before_mmu(uint64_t* page_table_base);
extern void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
                      uint64_t phys_start, uint64_t flags);
extern void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, 
                            uint64_t flags, const char* name);

// Dual-mapping functions for critical transition components
void map_vector_table_dual(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1, 
                           uint64_t vector_addr);
void map_range_dual_trampoline(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1,
                               uint64_t virt_low, uint64_t virt_high, 
                               uint64_t phys, uint64_t size);
extern char vector_table[];

// System register access functions
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

uint64_t read_mair_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(val));
    return val;
}

// Enhanced cache maintenance function
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

// Initialize the page tables for the kernel
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

// Return the kernel's L0 (top-level) page table
uint64_t* get_kernel_page_table(void) {
    return l0_table;
}

// Return the TTBR1 L0 page table for high virtual addresses
uint64_t* get_kernel_ttbr1_page_table(void) {
    return l0_table_ttbr1;
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

// OPTION D PHASE 2: Dual-mapping implementation for hybrid physical→virtual transition
void map_vector_table_dual(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1, 
                           uint64_t vector_addr) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Debug output
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = ':'; *uart = 'D'; *uart = 'U'; *uart = 'A'; *uart = 'L';
    *uart = '\r'; *uart = '\n';
    
    // Get CURRENT physical address from VBAR_EL1 (set by boot code using adrp)
    uint64_t vbar_phys = read_vbar_el1();
    uint64_t vect_phys_page = vbar_phys & ~0x7FFUL;  // 2KB align (not just page)
    
    // STEP 3 FIX: Use bitwise OR to preserve offset in high virtual address
    uint64_t vect_high = HIGH_VIRT_BASE | vect_phys_page;
    
    // Debug: Show physical and high virtual addresses
    *uart = 'P'; *uart = 'H'; *uart = 'Y'; *uart = 'S'; *uart = '=';
    uart_hex64_early(vect_phys_page);
    *uart = '\r'; *uart = '\n';
    *uart = 'H'; *uart = 'I'; *uart = 'G'; *uart = 'H'; *uart = '=';
    uart_hex64_early(vect_high);
    *uart = '\r'; *uart = '\n';
    
    // STEP 1: Identity map physical address in TTBR0 (for immediate post-MMU operation)
    // This ensures exceptions work right after MMU enable while VBAR still points to physical
    map_range(l0_table_ttbr0, vect_phys_page, vect_phys_page + 0x2000, 
              vect_phys_page, PTE_KERN_TEXT);
    *uart = 'I'; *uart = 'D'; *uart = 'E'; *uart = 'N'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // STEP 2: Map high virtual address in TTBR1 (for post-transition operation)
    // This is where VBAR will point after we transition to virtual addressing
    map_range(l0_table_ttbr1, vect_high, vect_high + 0x2000, 
              vect_phys_page, PTE_KERN_TEXT);
    *uart = 'H'; *uart = 'I'; *uart = 'G'; *uart = 'H'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // STEP 1 & 4: Explicit VBAR write before MMU (make it explicit in C)
    // STEP 5: Sanity check - verify 2KB alignment
    if ((vect_phys_page & 0x7FF) != 0) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'A'; *uart = 'L'; *uart = 'I'; *uart = 'G'; *uart = 'N';
        *uart = '\r'; *uart = '\n';
        while(1);  // Halt on alignment error
    }
    
    // Explicitly set VBAR to physical before MMU enable
    asm volatile("msr vbar_el1, %0\n\tisb" :: "r"(vect_phys_page));
    
    // STEP 4: Log first VBAR write
    *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':';
    *uart = 'L'; *uart = 'O'; *uart = 'W'; *uart = '=';
    uart_hex64_early(vect_phys_page);
    *uart = '\r'; *uart = '\n';
    
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
}

// Dual-mapping implementation for trampoline support
void map_range_dual_trampoline(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1,
                               uint64_t virt_low, uint64_t virt_high, 
                               uint64_t phys, uint64_t size) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
#if DEBUG_TRAMP_VALIDATE
    // DUAL-TRAMPOLINE VALIDATION: Show received parameters and computed map_range calls
    *uart = 'D'; *uart = 'U'; *uart = 'A'; *uart = 'L'; *uart = '.'; *uart = 'R'; *uart = 'E'; *uart = 'C'; *uart = 'V'; *uart = '\r'; *uart = '\n';
    *uart = 'L'; *uart = '='; uart_hex64_early(virt_low);
    *uart = 'H'; *uart = '='; uart_hex64_early(virt_high);  
    *uart = 'P'; *uart = '='; uart_hex64_early(phys);
    *uart = 'S'; *uart = '='; uart_hex64_early(size);
    *uart = '\r'; *uart = '\n';
    
    // Compute what we'll pass to map_range calls
    uint64_t ttbr0_start = virt_low;
    uint64_t ttbr0_end = virt_low + size;
    uint64_t ttbr0_pages = (ttbr0_end - ttbr0_start + PAGE_SIZE - 1) / PAGE_SIZE;
    
    uint64_t ttbr1_start = virt_high; 
    uint64_t ttbr1_end = virt_high + size;
    uint64_t ttbr1_pages = (ttbr1_end - ttbr1_start + PAGE_SIZE - 1) / PAGE_SIZE;
    
    *uart = 'T'; *uart = '0'; *uart = ':';
    *uart = 'S'; *uart = '='; uart_hex64_early(ttbr0_start);
    *uart = 'E'; *uart = '='; uart_hex64_early(ttbr0_end);
    *uart = 'N'; *uart = '='; uart_hex64_early(ttbr0_pages);
    *uart = '\r'; *uart = '\n';
    
    *uart = 'T'; *uart = '1'; *uart = ':';
    *uart = 'S'; *uart = '='; uart_hex64_early(ttbr1_start);
    *uart = 'E'; *uart = '='; uart_hex64_early(ttbr1_end);
    *uart = 'N'; *uart = '='; uart_hex64_early(ttbr1_pages);
    *uart = '\r'; *uart = '\n';
    
    // Warning for suspicious sizes
    if (ttbr0_pages > 0x100 || ttbr1_pages > 0x100) {
        *uart = 'D'; *uart = 'U'; *uart = 'A'; *uart = 'L'; *uart = '.';
        *uart = 'S'; *uart = 'I'; *uart = 'Z'; *uart = 'E'; *uart = '_';
        *uart = 'S'; *uart = 'U'; *uart = 'S'; *uart = 'P'; *uart = 'I'; *uart = 'C'; *uart = 'I'; *uart = 'O'; *uart = 'U'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
    }
#endif
    
    // Debug output
    *uart = 'D'; *uart = 'U'; *uart = 'A'; *uart = 'L'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Map to TTBR0 space (low virtual address - current PC space)
    map_range(l0_table_ttbr0, virt_low, virt_low + size, phys, PTE_KERN_TEXT);
    *uart = 'D'; *uart = 'L'; *uart = 'O'; *uart = 'W'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Map to TTBR1 space (high virtual address - kernel space)  
    map_range(l0_table_ttbr1, virt_high, virt_high + size, phys, PTE_KERN_TEXT);
    *uart = 'D'; *uart = 'H'; *uart = 'I'; *uart = 'G'; *uart = 'H'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    *uart = 'D'; *uart = 'U'; *uart = 'A'; *uart = 'L'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
}

// Enhanced function to enable the MMU with improved robustness
void enable_mmu_enhanced(uint64_t* page_table_base) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';

// ========================================================================
// CONSERVATIVE MIGRATION: PARALLEL POLICY TESTING
// Set to 1 to test new policy approach, 0 for original debug-rich approach
// ========================================================================
#define TEST_POLICY_APPROACH 0

#if TEST_POLICY_APPROACH
    // NEW POLICY-BASED APPROACH (preserving original code below)
    *uart = 'P'; *uart = 'O'; *uart = 'L'; *uart = 'I'; *uart = 'C'; *uart = 'Y'; *uart = ':'; *uart = 'T'; *uart = 'E'; *uart = 'S'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Get page table addresses for policy layer
    uint64_t page_table_phys_ttbr0 = (uint64_t)page_table_base;
    uint64_t page_table_phys_ttbr1 = (uint64_t)l0_table_ttbr1;
    
    // Test new policy-based MMU enable sequence
    int policy_result = mmu_apply_policy_and_enable(page_table_phys_ttbr0, page_table_phys_ttbr1);
    
    if (policy_result == 0) {
        *uart = 'P'; *uart = 'O'; *uart = 'L'; *uart = 'I'; *uart = 'C'; *uart = 'Y'; *uart = ':'; *uart = 'O'; *uart = 'K';
        *uart = '\r'; *uart = '\n';
        
        // Jump to continuation point using policy approach
        extern void mmu_continuation_point(void);
        uint64_t continuation_phys = (uint64_t)&mmu_continuation_point;
        void (*continuation_fn)(void) = (void(*)(void))continuation_phys;
        continuation_fn();
    } else {
        *uart = 'P'; *uart = 'O'; *uart = 'L'; *uart = 'I'; *uart = 'C'; *uart = 'Y'; *uart = ':'; *uart = 'F'; *uart = 'A'; *uart = 'I'; *uart = 'L';
        *uart = '\r'; *uart = '\n';
        return;
    }
#else
    // ORIGINAL APPROACH: Full debug infrastructure (ALL 1500+ lines preserved)
    *uart = 'O'; *uart = 'R'; *uart = 'I'; *uart = 'G'; *uart = ':'; *uart = 'D'; *uart = 'E'; *uart = 'B'; *uart = 'U'; *uart = 'G';
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
    
    // ✅ CRITICAL: Use bootstrap-dual TCR configuration for entire MMU init path
    // This ensures EPD0=0 (TTBR0 enabled) throughout the initialization sequence
    // After jumping to high VA, continuation function will switch to kernel-only mode
    mmu_configure_tcr_bootstrap_dual(VA_BITS_48 ? 48 : 39);
    
    // Get TCR value for assembly input (policy layer is authoritative source)
    uint64_t tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    
    // POLICY LAYER: Use authoritative MAIR configuration (eliminates duplication)
    mmu_configure_mair();
    
    // Get MAIR value for assembly input (policy layer is authoritative source)
    uint64_t mair;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(mair));
    
    // POLICY LAYER: Use authoritative TTBR configuration (eliminates duplication)
    mmu_set_ttbr_bases(page_table_phys_ttbr0, page_table_phys_ttbr1);
    
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
    
    // TRAMPOLINE SETUP: Dual-map trampoline section for atomic PC transition
    extern void mmu_trampoline_low(void);
    extern char _trampoline_section_start[], _trampoline_section_end[];
    uint64_t tramp_phys = (uint64_t)&mmu_trampoline_low;
    // STEP 5: Calculate size from labels (more reliable than .data symbol)
    uint64_t tramp_size = (uint64_t)(_trampoline_section_end - _trampoline_section_start);
    uint64_t tramp_high = HIGH_VIRT_BASE + tramp_phys;

#define DEBUG_TRAMP_VALIDATE 1
#if DEBUG_TRAMP_VALIDATE
    // TRAMPOLINE VALIDATION: Check symbol math and parameters
    *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P'; *uart = '.';
    *uart = 'S'; *uart = 'Y'; *uart = 'M'; *uart = ':'; *uart = '\r'; *uart = '\n';
    
    // Print raw symbol addresses
    *uart = 'L'; *uart = 'O'; *uart = 'W'; *uart = '='; uart_hex64_early((uint64_t)&mmu_trampoline_low);
    *uart = 'S'; *uart = 'I'; *uart = 'Z'; *uart = '='; uart_hex64_early(tramp_size);
    *uart = '\r'; *uart = '\n';
    
    // Compute parameters for dual mapping call
    uint64_t start_low = tramp_phys;
    uint64_t size_bytes = tramp_size;
    uint64_t end_low = start_low + size_bytes;
    uint64_t num_pages = (end_low - start_low + PAGE_SIZE - 1) / PAGE_SIZE;
    
    *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P'; *uart = '.';
    *uart = 'C'; *uart = 'A'; *uart = 'L'; *uart = 'L'; *uart = '\r'; *uart = '\n';
    *uart = 'S'; *uart = '='; uart_hex64_early(start_low);
    *uart = 'Z'; *uart = '='; uart_hex64_early(size_bytes);
    *uart = 'E'; *uart = '='; uart_hex64_early(end_low);
    *uart = 'N'; *uart = '='; uart_hex64_early(num_pages);
    *uart = '\r'; *uart = '\n';
    
    // Size warning check
    if (num_pages > 0x100) {
        *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P'; *uart = '.';
        *uart = 'S'; *uart = 'I'; *uart = 'Z'; *uart = 'E'; *uart = '_';
        *uart = 'S'; *uart = 'U'; *uart = 'S'; *uart = 'P'; *uart = 'I'; *uart = 'C'; *uart = 'I'; *uart = 'O'; *uart = 'U'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
        *uart = 'N'; *uart = '='; uart_hex64_early(num_pages);
        *uart = '\r'; *uart = '\n';
    }
#endif
    
    *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P'; *uart = ':'; *uart = 'S'; *uart = 'E'; *uart = 'T'; *uart = 'U'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
    
    // Dual-map trampoline at both TTBR0 and TTBR1 addresses
    map_range_dual_trampoline(page_table_base, l0_table_ttbr1, tramp_phys, tramp_high, tramp_phys, tramp_size);
    
    *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // VECTOR TABLE DUAL MAPPING: Ensure vector table is accessible in both address spaces
    extern char vector_table[];
    uint64_t vector_addr = (uint64_t)vector_table;
    
#if DEBUG_TRAMP_VALIDATE
    // VECTOR TABLE VALIDATION: Show vector table address and mapping details
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = 'T'; *uart = '.';
    *uart = 'A'; *uart = 'D'; *uart = 'D'; *uart = 'R'; *uart = '\r'; *uart = '\n';
    *uart = 'V'; *uart = '='; uart_hex64_early(vector_addr);
    *uart = '\r'; *uart = '\n';
    
    // Show page boundaries
    uint64_t vect_page_start = vector_addr & ~0xFFF;
    uint64_t vect_high = HIGH_VIRT_BASE + vect_page_start;
    *uart = 'L'; *uart = '='; uart_hex64_early(vect_page_start);        // Low mapping
    *uart = 'H'; *uart = '='; uart_hex64_early(vect_high);              // High mapping  
    *uart = '\r'; *uart = '\n';
#endif
    
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'E'; *uart = 'T'; *uart = 'U'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
    
    // Dual-map vector table at both TTBR0 (low) and TTBR1 (high) addresses  
    map_vector_table_dual(page_table_base, l0_table_ttbr1, vector_addr);
    
    *uart = 'V'; *uart = 'E'; *uart = 'C'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // POLICY LAYER: Pre-enable barrier sequence (eliminates duplication)
    mmu_barrier_sequence_pre_enable();
    
    // POLICY LAYER: Comprehensive TLB invalidation (eliminates duplication)
    mmu_comprehensive_tlbi_sequence();
    
    // NOTE: MMU enable instruction remains in assembly block for proper physical→virtual transition
    asm volatile (
        "assembly_start:\n"           // Label for PC detection
        // Save critical values
        "mov x19, %0\n"              // x19 = page_table_base_ttbr0
        "mov x18, %1\n"              // x18 = page_table_base_ttbr1
        "mov x20, %2\n"              // x20 = tcr value, hang at TLOW marker, likely due to EPD0=1 loaded into X20 before MMU enablement.
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
        
        // BREAKPOINT #1: End assembly block for policy layer register setup
        "mov w27, #'P'\n"            // 'P' = Policy layer taking over
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = pOlicy  
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = poLicy
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = Breakpoint 1
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // End assembly block - execution continues to policy layer calls
        
        : // No outputs from first assembly block  
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value (not used in this block)
          "r"(mair),                 // %3: MAIR_EL1 value (not used in this block)
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)  
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // ✅ POLICY LAYER CALLS - Replace register writes with centralized policy
    *uart = 'R'; *uart = 'E'; *uart = 'G'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Set up memory attributes (replacing msr mair_el1, x21)
    mmu_configure_mair();
    
    // Set up translation control for BOOTSTRAP DUAL-TABLE MODE
    // Use bootstrap_dual (EPD0=0, EPD1=0) instead of kernel_only (EPD0=1, EPD1=0)
    // This allows trampoline to enable MMU without modifying TCR (eliminates race condition)
    // After jumping to high VA, continuation function will switch to kernel_only mode
    mmu_configure_tcr_bootstrap_dual(VA_BITS_48 ? 48 : 39);
    
    // Set translation table bases (replacing msr ttbr0_el1, x19 and msr ttbr1_el1, x18)
    mmu_set_ttbr_bases(page_table_phys_ttbr0, page_table_phys_ttbr1);
    
    *uart = 'R'; *uart = 'E'; *uart = 'G'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // ✅ DIAGNOSTIC CHECKPOINT 1: Verify TCR immediately after configuration
    uint64_t tcr_checkpoint1;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr_checkpoint1));
    *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = '1'; *uart = ':';
    uart_hex64_early(tcr_checkpoint1);
    *uart = ' '; *uart = 'E'; *uart = 'P'; *uart = 'D'; *uart = '0'; *uart = ':';
    *uart = '0' + ((tcr_checkpoint1 >> 7) & 1);
    *uart = '\r'; *uart = '\n';
    
    // Resume assembly block for cache flush and debug infrastructure
    asm volatile (
        // PHASE 1: COMPREHENSIVE CACHE FLUSH FOR PAGE TABLE COHERENCY
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        
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
        
        // PHASE 2: TLB INVALIDATION - END ASSEMBLY BLOCK FOR POLICY LAYER
        "mov w27, #'P'\n"
        "str w27, [x26]\n"
        "mov w27, #'2'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // End assembly block to call policy layer for TLB operations
        
        : // No outputs from assembly block part 2A
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value
          "r"(mair),                 // %3: MAIR_EL1 value
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // ❌ ORIGINAL TLB OPERATIONS - COMMENTED OUT FOR POLICY VIOLATION FIXES
    // "tlbi vmalle1\n"             // ❌ LOCAL-ONLY VIOLATION - Invalidate all stage 1 TLB entries (local)
    // "dsb nsh\n"                  // Data synchronization barrier (non-shareable)  
    // "tlbi vmalle1is\n"           // ❌ INNER-SHAREABLE VIOLATION - Invalidate all stage 1 TLB entries (inner shareable)
    // "dsb ish\n"                  // Data synchronization barrier (inner shareable)
    // "ic iallu\n"                 // Invalidate instruction cache (all)
    // "dsb sy\n"                   // Wait for instruction cache invalidation
    // "isb\n"                      // Instruction synchronization barrier
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence  
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = '1'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    mmu_comprehensive_tlbi_sequence();
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = '1'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Resume assembly block for MMU enable operations
    asm volatile (
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
        
        // DEAD CODE: x23 comprehensive computation (COMMENTED OUT - Step 1)
        // "mrs x23, sctlr_el1\n"       // Read current firmware SCTLR_EL1
        // 
        // // PRESERVE RES1 bits, only modify specific control bits
        // "mov w27, #'S'\n"            // 'S' = Setting (preserving firmware base)
        // "str w27, [x26]\n"
        // "mov w27, #'E'\n"
        // "str w27, [x26]\n"
        // "mov w27, #'T'\n"
        // "str w27, [x26]\n"
        // "mov w27, #':'\n"
        // "str w27, [x26]\n"
        // 
        // // Only set the bits we need, preserving all RES1 bits
        // "orr x23, x23, #0x1000\n"    // Set I(12) - instruction cache
        // "orr x23, x23, #0x4\n"       // Set C(2) - data cache
        // "orr x23, x23, #0x1\n"       // Set M(0) - MMU enable
        
        // DEAD CODE: x23 debug output (COMMENTED OUT - Step 1)  
        // "mov w27, #'F'\n"            // 'F' = Final SCTLR
        // "str w27, [x26]\n"
        // "mov w27, #'I'\n"
        // "str w27, [x26]\n"
        // "mov w27, #'N'\n"
        // "str w27, [x26]\n"
        // "mov w27, #':'\n"
        // "str w27, [x26]\n"
        // // Output our SCTLR value (simplified hex output)
        // "and x29, x23, #0xFFFF\n"    // Get low 16 bits
        // "lsr x29, x29, #12\n"        // Show bits 12-15 (I,Z,C,A bits)
        // "and x29, x29, #0xF\n"       // Just 4 bits
        // "cmp x29, #10\n"
        // "b.lt 52f\n"
        // "add x29, x29, #'A'-10\n"
        // "b 53f\n"
        // "52:\n"
        // "add x29, x29, #'0'\n"
        // "53:\n"
        // "str w29, [x26]\n"
        
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
        // STEP 1: Re-seed TTBR0 base register (fix register corruption!)
        "mov x19, %0\n"              // Reload TTBR0 base from input operand
        // STEP 2: Use freshly loaded register for dereference
        "ldr x28, [x19]\n"           // Read L0 table entry 0 (now safe!)
        // STEP 3: Breadcrumb to confirm dereference succeeded
        "mov w27, #'l'\n"            // lowercase 'l' to distinguish
        "str w27, [x26]\n"
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
        
        // STEP 2 FIX: Check if vector table mapping exists at ACTUAL VBAR address
        "mrs x28, vbar_el1\n"        // Get actual VBAR address (not hardcoded!)
        "lsr x29, x28, #39\n"        // L0 index
        "and x29, x29, #0x1FF\n"
        "ldr x30, [x19, x29, lsl #3]\n" // Load L0 entry
        "tst x30, #1\n"              // Check valid bit
        "b.eq 21f\n"                 // Branch if invalid
        
        // Vector table mapping exists at VBAR address
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
        "lsr x28, x28, #6\n"         // Shift DAIF bits [9:6] to [3:0]
        "and x28, x28, #0xF\n"       // Mask to get 4 DAIF bits
        
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
        
        // PRE-MMU CPU STATE DUMP - 4 Critical Missing Pieces
        "mov w27, #'C'\n"            // 'C' = CPU state
        "str w27, [x26]\n"
        "mov w27, #'P'\n"            // 'P' = cPu
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = cpU
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // 1. Current Exception Level
        "mrs x28, currentel\n"       // Get current exception level
        "lsr x28, x28, #2\n"         // Extract EL bits [3:2]
        "and x28, x28, #0x3\n"       // Mask to 2 bits (EL0-EL3)
        "add w27, w28, #'0'\n"       // Convert EL to ASCII (should be '1' for EL1)
        "str w27, [x26]\n"
        
        // 2. Current SCTLR_EL1 State (before our modification)
        "mov w27, #'S'\n"            // 'S' = SCTLR
        "str w27, [x26]\n"
        "mrs x28, sctlr_el1\n"       // Read current SCTLR_EL1
        
        // Show critical bits: M(0), C(2), I(12)
        "and x29, x28, #0x1\n"       // Extract M bit (MMU enable)
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"           // Should be '0' (MMU disabled)
        
        "and x29, x28, #0x4\n"       // Extract C bit (data cache)
        "lsr x29, x29, #2\n"         // Shift to bit 0
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        "and x29, x28, #0x1000\n"    // Extract I bit (instruction cache)
        "lsr x29, x29, #12\n"        // Shift to bit 0
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // 3. Stack Pointer State
        "mov w27, #'P'\n"            // 'P' = sP (stack pointer)
        "str w27, [x26]\n"
        "mov x28, sp\n"              // Get current stack pointer
        "and x29, x28, #0xF\n"       // Check 16-byte alignment
        "add w27, w29, #'0'\n"       // Convert alignment to ASCII (should be '0')
        "str w27, [x26]\n"
        
        // 4. DAIF (Interrupt Mask) State
        "mov w27, #'I'\n"            // 'I' = Interrupts (DAIF)
        "str w27, [x26]\n"
        "mrs x28, daif\n"            // Get interrupt mask state
        
        // Show key interrupt bits: D(9), A(8), I(7), F(6) - CORRECTED bit positions
        "lsr x29, x28, #6\n"         // Shift DAIF bits [9:6] to [3:0]
        "and x29, x29, #0xF\n"       // Mask to get 4 DAIF bits
        
        // Extract D bit (bit 3 of shifted value = original bit 9)
        "and x30, x29, #0x8\n"       // Extract D bit (debug exceptions)
        "lsr x30, x30, #3\n"         // Shift to bit 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // Extract A bit (bit 2 of shifted value = original bit 8)
        "and x30, x29, #0x4\n"       // Extract A bit (async aborts)
        "lsr x30, x30, #2\n"         // Shift to bit 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // Extract I bit (bit 1 of shifted value = original bit 7)
        "and x30, x29, #0x2\n"       // Extract I bit (IRQ)
        "lsr x30, x30, #1\n"         // Shift to bit 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // Extract F bit (bit 0 of shifted value = original bit 6)
        "and x30, x29, #0x1\n"       // Extract F bit (FIQ)
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // STEP 4A: **MINIMAL SCTLR_EL1 CONFIGURATION TEST** (Option 4A)
        // Test with absolute minimum SCTLR_EL1 settings first
        "mov w27, #'4'\n"
        "str w27, [x26]\n"
        "mov w27, #'A'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Read current SCTLR_EL1 to preserve RES1 bits
        "mrs x28, sctlr_el1\n"
        
        // SAFE: Preserve all RES1 bits, only add MMU enable
        // Note: Firmware may already have M=0, C=0, I=0, so just add M=1
        // Do NOT use bic instructions as they can clear RES1 bits
        
        // Set ONLY the MMU enable bit (safest approach)
        "orr x28, x28, #0x1\n"       // Set M bit (MMU enable) - preserve everything else
        
        // Debug: Show minimal SCTLR value we're about to test
        "mov w27, #'M'\n"            // 'M' = Minimal
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = Initial
        "str w27, [x26]\n"
        "mov w27, #'N'\n"            // 'N' = miNimal
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Output minimal SCTLR value (simplified - just show if MMU bit is set)
        "and x29, x28, #0x1\n"       // Check MMU bit
        "add w27, w29, #'0'\n"       // Convert to ASCII (should be '1')
        "str w27, [x26]\n"
        
        // CRITICAL: TLB maintenance - END ASSEMBLY BLOCK FOR POLICY LAYER
        "mov w27, #'T'\n"            // 'T' = TLB
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = tLb
        "str w27, [x26]\n"
        "mov w27, #'B'\n"            // 'B' = tlB
        "str w27, [x26]\n"
        "mov w27, #'2'\n"            // '2' = Second TLB operation
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // End assembly block to call policy layer for second TLB operation
        
        : // No outputs from assembly block part 2B  
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value
          "r"(mair),                 // %3: MAIR_EL1 value
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // ❌ ORIGINAL TLB OPERATIONS - COMMENTED OUT FOR POLICY VIOLATION FIXES
    // "dsb sy\n"                   // Full system data synchronization
    // "tlbi vmalle1\n"             // ❌ LOCAL-ONLY VIOLATION - Invalidate all TLB entries  
    // "dsb ish\n"                  // Data synchronization barrier (inner shareable)
    // "ic iallu\n"                 // Invalidate instruction cache
    // "dsb sy\n"                   // Wait for instruction cache invalidation
    // "isb\n"                      // Instruction synchronization barrier
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = '2'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    mmu_comprehensive_tlbi_sequence();
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = '2'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Resume assembly block for minimal MMU enable test
    asm volatile (
        // TEST 1: Try minimal MMU enable (MMU bit only, no caches)
        "mov w27, #'T'\n"            // 'T' = Test
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = Test 1
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // CRITICAL: Disable ALL interrupts before MMU enable
        "mov w27, #'D'\n"            // 'D' = Disable interrupts marker
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = Interrupts
        "str w27, [x26]\n"
        "mov w27, #'S'\n"            // 'S' = diSable
        "str w27, [x26]\n"
        
        "msr daifset, #15\n"         // Disable ALL interrupts (D,A,I,F bits)
        "isb\n"                      // Synchronize interrupt state change
        
        // Debug: Read back DAIF and show each bit individually
        "mrs x29, daif\n"            // Read back DAIF register
        "mov w27, #'D'\n"            // 'D' = DAIF debug marker
        "str w27, [x26]\n"
        "mov w27, #'A'\n"            // 'A' = dAif
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = daIf  
        "str w27, [x26]\n"
        "mov w27, #'F'\n"            // 'F' = daiF
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // CORRECTED: Shift DAIF bits [9:6] to [3:0] first
        "lsr x28, x29, #6\n"         // Shift DAIF bits [9:6] to [3:0]
        "and x28, x28, #0xF\n"       // Mask to get 4 DAIF bits
        
        // Output D bit (bit 3 of shifted value = original bit 9)
        "and x30, x28, #0x8\n"       // Extract D bit (debug exceptions)
        "lsr x30, x30, #3\n"         // Shift to position 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"           // Output D bit (should be '1' if disabled)
        
        // Output A bit (bit 2 of shifted value = original bit 8)
        "and x30, x28, #0x4\n"       // Extract A bit (async aborts)
        "lsr x30, x30, #2\n"         // Shift to position 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"           // Output A bit (should be '1' if disabled)
        
        // Output I bit (bit 1 of shifted value = original bit 7)
        "and x30, x28, #0x2\n"       // Extract I bit (IRQ)
        "lsr x30, x30, #1\n"         // Shift to position 0
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"           // Output I bit (should be '1' if disabled)
        
        // Output F bit (bit 0 of shifted value = original bit 6)
        "and x30, x28, #0x1\n"       // Extract F bit (FIQ)
        "add w27, w30, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"           // Output F bit (should be '1' if disabled)
        
        // Check if ALL interrupts are properly disabled (use corrected DAIF bits)
        "and x30, x28, #0xF\n"       // Get all 4 DAIF bits (x28 contains shifted bits)
        "cmp x30, #0xF\n"            // Should be 1111 (all disabled)
        "beq interrupts_fully_disabled\n"
        
        // Partial or no interrupt disable - show what we got vs what we expected
        "mov w27, #'P'\n"            // 'P' = Partial disable
        "str w27, [x26]\n"
        "mov w27, #'A'\n"            // 'A' = pArtial
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = paRtial
        "str w27, [x26]\n"
        "mov w27, #'T'\n"            // 'T' = parT
        "str w27, [x26]\n"
        
        // Show expected vs actual
        "mov w27, #'E'\n"            // 'E' = Expected
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = 1111 expected
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        "mov w27, #'1'\n"
        "str w27, [x26]\n"
        
        "mov w27, #'G'\n"            // 'G' = Got
        "str w27, [x26]\n"
        
        // Re-output the actual DAIF bits we got (for comparison) - CORRECTED
        "and x30, x28, #0x8\n"       // D bit (from shifted value)
        "lsr x30, x30, #3\n"
        "add w27, w30, #'0'\n"
        "str w27, [x26]\n"
        "and x30, x28, #0x4\n"       // A bit (from shifted value)
        "lsr x30, x30, #2\n"
        "add w27, w30, #'0'\n"
        "str w27, [x26]\n"
        "and x30, x28, #0x2\n"       // I bit (from shifted value)
        "lsr x30, x30, #1\n"
        "add w27, w30, #'0'\n"
        "str w27, [x26]\n"
        "and x30, x28, #0x1\n"       // F bit (from shifted value)
        "add w27, w30, #'0'\n"
        "str w27, [x26]\n"
        
        // Proceed anyway - try MMU enable even with partial interrupt disable
        "mov w27, #'C'\n"            // 'C' = Continue anyway
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = cOntinue
        "str w27, [x26]\n"
        "mov w27, #'N'\n"            // 'N' = coNtinue
        "str w27, [x26]\n"
        "b attempt_mmu_enable\n"     // Jump to MMU enable attempt
        
        "interrupts_fully_disabled:\n"
        "mov w27, #'F'\n"            // 'F' = Full disable success
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = fUll
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = fuLl
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = fulL
        "str w27, [x26]\n"
        
        "attempt_mmu_enable:\n"
        // **OPTION A: QEMU DAIF BUG BYPASS - TIMEOUT-BASED MMU ENABLE**
        // Since QEMU can't disable interrupts properly, use timeout/retry approach
        "mov w27, #'M'\n"            // 'M' = MMU enable attempt marker
        "str w27, [x26]\n"
        "mov w27, #'M'\n"            // 'M' = MMU
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = mmU
        "str w27, [x26]\n"
        
        // STAGE 1: Setup timeout counter for MMU enable retry loop
        "mov w27, #'T'\n"            // 'T' = Timeout setup
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = timeOut
        "str w27, [x26]\n"
        "mov w27, #'S'\n"            // 'S' = Setup
        "str w27, [x26]\n"
        
        "mov x30, #0x50000\n"        // Large timeout counter (320K iterations)
        
        // STAGE 2: Pre-timeout state verification
        "mov w27, #'P'\n"            // 'P' = Pre-timeout verification
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = pRe
        "str w27, [x26]\n"
        "mov w27, #'E'\n"            // 'E' = prE
        "str w27, [x26]\n"
        
        // Verify SCTLR state before timeout loop
        "mrs x29, sctlr_el1\n"       // Read current SCTLR_EL1
        "and x0, x29, #0x1\n"        // Check if MMU is already enabled
        "cbnz x0, already_enabled\n" // Branch if MMU already enabled
        
        // PRIORITY 1: Single MMU enable attempt (no retry loop)
        "mov w27, #'S'\n"            // 'S' = Single attempt
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = sIngle
        "str w27, [x26]\n"
        "mov w27, #'N'\n"            // 'N' = siNgle
        "str w27, [x26]\n"
        "mov w27, #'G'\n"            // 'G' = sinGle
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // PRIORITY 2: Comprehensive Register Diagnostic Dump
        "mov w27, #'D'\n"            // 'D' = Diagnostic dump
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = dIagnostic
        "str w27, [x26]\n"
        "mov w27, #'A'\n"            // 'A' = diAgnostic
        "str w27, [x26]\n"
        "mov w27, #'G'\n"            // 'G' = diaGnostic
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // 1. CurrentEL Detection and Output
        "mrs x0, currentel\n"        // Get current exception level
        "lsr x0, x0, #2\n"           // Extract EL bits [3:2]
        "and x0, x0, #0x3\n"         // Mask to 2 bits (EL0-EL3)
        "mov w27, #'E'\n"            // 'E' = Exception Level
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = eL
        "str w27, [x26]\n"
        "add w27, w0, #'0'\n"        // Convert EL to ASCII
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // 2. SCTLR_EL1 Readback (confirm what we actually wrote)
        "mrs x1, sctlr_el1\n"        // Re-read SCTLR_EL1
        "mov w27, #'S'\n"            // 'S' = SCTLR
        "str w27, [x26]\n"
        "mov w27, #'C'\n"            // 'C' = sCtlr
        "str w27, [x26]\n"
        "mov w27, #'T'\n"            // 'T' = scTlr
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = sctR
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Output SCTLR_EL1 M bit status
        "and x2, x1, #0x1\n"         // Extract M bit
        "add w27, w2, #'0'\n"        // Convert to ASCII (should be '0')
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // 3. Exception Level Conditional Logic
        "cmp x0, #2\n"               // Check if CurrentEL == 2 (EL2)
        "b.eq el2_diagnostics\n"     // Branch to EL2-specific diagnostics
        "cmp x0, #1\n"               // Check if CurrentEL == 1 (EL1)
        "b.eq el1_diagnostics\n"     // Branch to EL1-specific diagnostics
        
        // Unexpected exception level
        "mov w27, #'U'\n"            // 'U' = Unexpected EL
        "str w27, [x26]\n"
        "mov w27, #'N'\n"            // 'N' = uNexpected
        "str w27, [x26]\n"
        "mov w27, #'K'\n"            // 'K' = unKnown
        "str w27, [x26]\n"
        "b diagnostic_complete\n"
        
        "el2_diagnostics:\n"
        // Running at EL2 - check HCR_EL2 and banking
        "mov w27, #'H'\n"            // 'H' = HCR_EL2
        "str w27, [x26]\n"
        "mov w27, #'C'\n"            // 'C' = hCr
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = hcR
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        "mrs x3, hcr_el2\n"          // Read HCR_EL2 (safe at EL2)
        
        // Check E2H bit (bit 34)
        "and x4, x3, #(1 << 34)\n"   // Extract E2H bit
        "cbnz x4, e2h_enabled\n"     // Branch if E2H=1
        
        // E2H=0: Normal EL2, SCTLR_EL1 should work
        "mov w27, #'N'\n"            // 'N' = Normal EL2
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = nOrmal
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = noRmal
        "str w27, [x26]\n"
        "b check_tvm_bit\n"
        
        "e2h_enabled:\n"
        // E2H=1: VHE mode, SCTLR_EL12 is the effective register
        "mov w27, #'V'\n"            // 'V' = VHE mode
        "str w27, [x26]\n"
        "mov w27, #'H'\n"            // 'H' = vHe
        "str w27, [x26]\n"
        "mov w27, #'E'\n"            // 'E' = vhE
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // NOTE: SCTLR_EL12 access removed due to Cortex-A53 limitation
        // Cortex-A53 (ARMv8.0-A) doesn't support VHE/sctlr_el12
        // Indicate VHE detection but can't read effective register
        "mov w27, #'N'\n"            // 'N' = Not supported on this CPU
        "str w27, [x26]\n"
        "mov w27, #'S'\n"            // 'S' = not Supported
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = not sUpported
        "str w27, [x26]\n"
        "mov w27, #'P'\n"            // 'P' = not suPported
        "str w27, [x26]\n"
        "b diagnostic_complete\n"
        
        "check_tvm_bit:\n"
        // Check TVM bit (bit 26) for VM control trapping
        "and x4, x3, #(1 << 26)\n"   // Extract TVM bit
        "cbnz x4, tvm_trapping\n"    // Branch if TVM=1
        
        "mov w27, #'T'\n"            // 'T' = TVM=0 (no trapping)
        "str w27, [x26]\n"
        "mov w27, #'V'\n"            // 'V' = tVm
        "str w27, [x26]\n"
        "mov w27, #'0'\n"            // '0' = TVM=0
        "str w27, [x26]\n"
        "b diagnostic_complete\n"
        
        "tvm_trapping:\n"
        "mov w27, #'T'\n"            // 'T' = TVM=1 (trapping enabled)
        "str w27, [x26]\n"
        "mov w27, #'V'\n"            // 'V' = tVm  
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = TVM=1
        "str w27, [x26]\n"
        "b diagnostic_complete\n"
        
        "el1_diagnostics:\n"
        // Running at EL1 - normal case, but check for hypervisor interference
        "mov w27, #'E'\n"            // 'E' = EL1 mode
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = eL1
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = el1
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Attempt to read HCR_EL2 (may fault at EL1)
        "mov w27, #'H'\n"            // 'H' = Attempting HCR_EL2 read
        "str w27, [x26]\n"
        "mov w27, #'C'\n"            // 'C' = hCr
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = hcR
        "str w27, [x26]\n"
        "mov w27, #'?'\n"            // '?' = Unknown (may fault)
        "str w27, [x26]\n"
        
        // TODO: Add exception handling for HCR_EL2 read at EL1
        // For now, skip HCR_EL2 read to avoid fault
        "b diagnostic_complete\n"
        
        "diagnostic_complete:\n"
        // End of diagnostic dump
        "mov w27, #'\\r'\n"
        "str w27, [x26]\n"
        "mov w27, #'\\n'\n"
        "str w27, [x26]\n"
        
        // ASSEMBLY CONTEXT: MMU already enabled by policy layer - verify status
        "mov w27, #'A'\n"            // 'A' = Assembly context
        "str w27, [x26]\n"
        "mov w27, #'S'\n"            // 'S' = aSsembly  
        "str w27, [x26]\n"
        "mov w27, #'M'\n"            // 'M' = sMmu
        "str w27, [x26]\n"
        "mov w27, #':'\n"            // → "ASM:"
        "str w27, [x26]\n"
        
        // BREAKPOINT #2: End assembly block for policy layer MMU enable
        "mov w27, #'P'\n"            // 'P' = Policy layer taking over
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = pOlicy  
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = poLicy
        "str w27, [x26]\n"
        "mov w27, #'2'\n"            // '2' = Breakpoint 2
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // End assembly block - execution continues to policy layer MMU enable
        
        : // No outputs from second assembly block
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value
          "r"(mair),                 // %3: MAIR_EL1 value
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // ✅ POLICY LAYER CALL - Replace SCTLR write with centralized policy
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // ✅ DIAGNOSTIC CHECKPOINT 2: Verify TCR right before trampoline jump
    uint64_t tcr_checkpoint2;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr_checkpoint2));
    *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = '2'; *uart = ':';
    uart_hex64_early(tcr_checkpoint2);
    *uart = ' '; *uart = 'E'; *uart = 'P'; *uart = 'D'; *uart = '0'; *uart = ':';
    *uart = '0' + ((tcr_checkpoint2 >> 7) & 1);
    *uart = '\r'; *uart = '\n';
    
    // DIAGNOSTIC: Verify trampoline identity mapping exists in TTBR0 table
    uint64_t tramp_test_addr = tramp_phys;
    uint64_t* test_l3 = get_l3_table_for_addr(page_table_base, tramp_test_addr);
    if (test_l3) {
        uint64_t l3_idx = (tramp_test_addr >> 12) & 0x1FF;
        uint64_t pte = test_l3[l3_idx];
        *uart = 'P'; *uart = 'T'; *uart = 'E'; *uart = ':';
        uart_hex64_early(pte);
        *uart = ' '; *uart = 'V'; *uart = ':';
        *uart = '0' + ((pte & PTE_VALID) ? 1 : 0);
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'N'; *uart = 'O'; *uart = 'L'; *uart = '3'; *uart = '!';
        *uart = '\r'; *uart = '\n';
    }

    // DIAGNOSTIC: Verify stack is identity-mapped
    uint64_t current_sp;
    __asm__ volatile("mov %0, sp" : "=r"(current_sp));
    uint64_t sp_page = current_sp & ~0xFFF;
    uint64_t* sp_l3 = get_l3_table_for_addr(page_table_base, sp_page);
    if (sp_l3) {
        uint64_t sp_l3_idx = (sp_page >> 12) & 0x1FF;
        uint64_t sp_pte = sp_l3[sp_l3_idx];
        *uart = 'S'; *uart = 'P'; *uart = ':';
        uart_hex64_early(current_sp);
        *uart = ' '; *uart = 'P'; *uart = 'T'; *uart = 'E'; *uart = ':';
        uart_hex64_early(sp_pte);
        *uart = ' '; *uart = 'V'; *uart = ':';
        *uart = '0' + ((sp_pte & PTE_VALID) ? 1 : 0);
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'S'; *uart = 'P'; *uart = ':'; *uart = 'N'; *uart = 'O'; *uart = 'L'; *uart = '3'; *uart = '!';
        *uart = '\r'; *uart = '\n';
    }

    // CRITICAL DIAGNOSTIC: Dump FULL translation path for UART (0x09000000)
    // We need to verify L0→L1→L2→L3 entries, not just L3!
    #define UART_PHYS_DIAG 0x09000000UL
    {
        // Calculate indices for 48-bit VA, 4KB granule
        uint64_t va = UART_PHYS_DIAG;
        uint64_t l0_idx = (va >> 39) & 0x1FF;  // Should be 0
        uint64_t l1_idx = (va >> 30) & 0x1FF;  // Should be 0
        uint64_t l2_idx = (va >> 21) & 0x1FF;  // Should be 72
        uint64_t l3_idx = (va >> 12) & 0x1FF;  // Should be 0
        
        *uart = 'U'; *uart = 'W'; *uart = 'A'; *uart = 'L'; *uart = 'K'; *uart = ':';
        *uart = '\r'; *uart = '\n';
        
        // L0 entry
        uint64_t* l0_table = page_table_base;
        uint64_t l0_entry = l0_table[l0_idx];
        *uart = 'L'; *uart = '0'; *uart = '[';
        *uart = '0' + l0_idx;
        *uart = ']'; *uart = '=';
        uart_hex64_early(l0_entry);
        *uart = ' '; *uart = 'V'; *uart = ':';
        *uart = '0' + ((l0_entry & 0x1) ? 1 : 0);
        *uart = ' '; *uart = 'T'; *uart = ':';
        *uart = '0' + ((l0_entry & 0x2) ? 1 : 0);  // Table bit
        *uart = '\r'; *uart = '\n';
        
        if ((l0_entry & 0x3) != 0x3) {
            *uart = 'L'; *uart = '0'; *uart = ':'; *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
            *uart = '\r'; *uart = '\n';
        } else {
            // L1 entry
            uint64_t* l1_table = (uint64_t*)(l0_entry & 0x0000FFFFFFFFF000UL);
            uint64_t l1_entry = l1_table[l1_idx];
            *uart = 'L'; *uart = '1'; *uart = '[';
            *uart = '0' + l1_idx;
            *uart = ']'; *uart = '=';
            uart_hex64_early(l1_entry);
            *uart = ' '; *uart = 'V'; *uart = ':';
            *uart = '0' + ((l1_entry & 0x1) ? 1 : 0);
            *uart = ' '; *uart = 'T'; *uart = ':';
            *uart = '0' + ((l1_entry & 0x2) ? 1 : 0);
            *uart = '\r'; *uart = '\n';
            
            if ((l1_entry & 0x3) != 0x3) {
                *uart = 'L'; *uart = '1'; *uart = ':'; *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
                *uart = '\r'; *uart = '\n';
            } else {
                // L2 entry
                uint64_t* l2_table = (uint64_t*)(l1_entry & 0x0000FFFFFFFFF000UL);
                uint64_t l2_entry = l2_table[l2_idx];
                *uart = 'L'; *uart = '2'; *uart = '[';
                // Print l2_idx as decimal (72 = 7*10+2)
                *uart = '0' + (l2_idx / 10);
                *uart = '0' + (l2_idx % 10);
                *uart = ']'; *uart = '=';
                uart_hex64_early(l2_entry);
                *uart = ' '; *uart = 'V'; *uart = ':';
                *uart = '0' + ((l2_entry & 0x1) ? 1 : 0);
                *uart = ' '; *uart = 'T'; *uart = ':';
                *uart = '0' + ((l2_entry & 0x2) ? 1 : 0);
                *uart = '\r'; *uart = '\n';
                
                if ((l2_entry & 0x3) != 0x3) {
                    *uart = 'L'; *uart = '2'; *uart = ':'; *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
                    *uart = '\r'; *uart = '\n';
                } else {
                    // L3 entry (page entry)
                    uint64_t* l3_table = (uint64_t*)(l2_entry & 0x0000FFFFFFFFF000UL);
                    uint64_t l3_entry = l3_table[l3_idx];
                    *uart = 'L'; *uart = '3'; *uart = '[';
                    *uart = '0' + l3_idx;
                    *uart = ']'; *uart = '=';
                    uart_hex64_early(l3_entry);
                    *uart = ' '; *uart = 'V'; *uart = ':';
                    *uart = '0' + ((l3_entry & 0x1) ? 1 : 0);
                    *uart = ' '; *uart = 'P'; *uart = ':';
                    *uart = '0' + ((l3_entry & 0x2) ? 1 : 0);  // Page bit
                    *uart = '\r'; *uart = '\n';
                    
                    // Verify PA matches
                    uint64_t pte_pa = l3_entry & 0x0000FFFFFFFFF000UL;
                    *uart = 'P'; *uart = 'A'; *uart = ':';
                    uart_hex64_early(pte_pa);
                    if (pte_pa == UART_PHYS_DIAG) {
                        *uart = ' '; *uart = 'O'; *uart = 'K';
                    } else {
                        *uart = ' '; *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
                    }
                    *uart = '\r'; *uart = '\n';
                }
            }
        }
    }

    // Check L1[1] for trampoline/vector range (1GB-2GB)
    uint64_t* l0_table = (uint64_t*)page_table_base;
    uint64_t l1_table_addr = l0_table[0] & ~0xFFFUL;
    uint64_t* l1_table = (uint64_t*)l1_table_addr;
    uint64_t l1_entry_1 = l1_table[1];  // L1[1] for addresses >= 1GB

    *uart = 'L'; *uart = '1'; *uart = '['; *uart = '1'; *uart = ']'; *uart = '=';
    uart_hex64_early(l1_entry_1);
    *uart = ' '; *uart = 'V'; *uart = ':';
    *uart = '0' + ((l1_entry_1 & 1) ? 1 : 0);  // Valid bit
    *uart = ' '; *uart = 'T'; *uart = ':';
    *uart = '0' + ((l1_entry_1 & 2) ? 1 : 0);  // Table bit
    *uart = '\r'; *uart = '\n';

    // FULL TRAMPOLINE WALK: Verify L2[0] and L3[144] for address 0x40090000
    // This verifies the COMPLETE translation path for the trampoline code
    if ((l1_entry_1 & 0x3) == 0x3) {  // L1[1] is valid table entry
        // Get L2 table address from L1[1]
        uint64_t l2_table_addr = l1_entry_1 & 0x0000FFFFFFFFF000UL;
        uint64_t* l2_table = (uint64_t*)l2_table_addr;
        
        // Force cache flush for L2 table before reading
        __asm__ volatile("dc civac, %0\n\tdsb sy\n\tisb" :: "r"(l2_table_addr) : "memory");
        
        // L2 index for 0x40090000 = (0x40090000 >> 21) & 0x1FF = 0
        uint64_t l2_entry_0 = l2_table[0];
        
        *uart = 'L'; *uart = '2'; *uart = '['; *uart = '0'; *uart = ']'; *uart = '=';
        uart_hex64_early(l2_entry_0);
        *uart = ' '; *uart = 'V'; *uart = ':';
        *uart = '0' + ((l2_entry_0 & 1) ? 1 : 0);
        *uart = ' '; *uart = 'T'; *uart = ':';
        *uart = '0' + ((l2_entry_0 & 2) ? 1 : 0);
        *uart = '\r'; *uart = '\n';
        
        if ((l2_entry_0 & 0x3) == 0x3) {  // L2[0] is valid table entry
            // Get L3 table address from L2[0]
            uint64_t l3_table_addr = l2_entry_0 & 0x0000FFFFFFFFF000UL;
            uint64_t* l3_table = (uint64_t*)l3_table_addr;
            
            // Force cache flush for L3 table before reading
            __asm__ volatile("dc civac, %0\n\tdsb sy\n\tisb" :: "r"(l3_table_addr) : "memory");
            
            // L3 index for 0x40090000 = (0x40090000 >> 12) & 0x1FF = 0x90 = 144
            uint64_t l3_entry_144 = l3_table[144];
            
            *uart = 'L'; *uart = '3'; *uart = '['; 
            *uart = '1'; *uart = '4'; *uart = '4';  // Index 144
            *uart = ']'; *uart = '=';
            uart_hex64_early(l3_entry_144);
            *uart = ' '; *uart = 'V'; *uart = ':';
            *uart = '0' + ((l3_entry_144 & 1) ? 1 : 0);
            *uart = ' '; *uart = 'P'; *uart = ':';
            *uart = '0' + ((l3_entry_144 & 2) ? 1 : 0);  // Page bit for L3
            *uart = '\r'; *uart = '\n';
            
            // Verify physical address matches trampoline
            uint64_t l3_pa = l3_entry_144 & 0x0000FFFFFFFFF000UL;
            *uart = 'T'; *uart = 'R'; *uart = 'M'; *uart = 'P'; *uart = '_'; *uart = 'P'; *uart = 'A'; *uart = ':';
            uart_hex64_early(l3_pa);
            if (l3_pa == (tramp_phys & ~0xFFFUL)) {
                *uart = ' '; *uart = 'O'; *uart = 'K';
            } else {
                *uart = ' '; *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
            }
            *uart = '\r'; *uart = '\n';
        } else {
            *uart = 'L'; *uart = '2'; *uart = '['; *uart = '0'; *uart = ']'; *uart = ':';
            *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
            *uart = '\r'; *uart = '\n';
        }
    } else {
        *uart = 'L'; *uart = '1'; *uart = '['; *uart = '1'; *uart = ']'; *uart = ':';
        *uart = 'B'; *uart = 'A'; *uart = 'D'; *uart = '!';
        *uart = '\r'; *uart = '\n';
    }
        
    // Jump to trampoline for atomic PC transition TTBR0→TTBR1
    *uart = 'J'; *uart = 'M'; *uart = 'P'; *uart = ':'; *uart = 'T'; *uart = 'R'; *uart = 'A'; *uart = 'M'; *uart = 'P';
    *uart = '\r'; *uart = '\n';
    
    // Assembly jump to trampoline (will not return here)
    asm volatile("b mmu_trampoline_low");
    
    // We should never reach this point
    *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'U'; *uart = 'N'; *uart = 'R'; *uart = 'E'; *uart = 'A'; *uart = 'C'; *uart = 'H';
    *uart = '\r'; *uart = '\n';
    
    // Resume assembly block for verification and continuation logic
    asm volatile (
        // Assembly context verification: Check if MMU enable just executed successfully
        "mrs x29, sctlr_el1\n"       // Read SCTLR_EL1 in assembly context
        "and x0, x29, #0x1\n"        // Check if MMU bit is set
        "cbnz x0, mmu_verify_success\n" // Branch if M=1 (MMU enable succeeded)
        
        // MMU enable failed - show failure marker
        "mov w27, #'F'\n"            // 'F' = Failed
        "str w27, [x26]\n"
        "mov w27, #'A'\n"            // 'A' = fAiled
        "str w27, [x26]\n"
        "mov w27, #'I'\n"            // 'I' = faIled
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = faiLed
        "str w27, [x26]\n"
        "b test_progressive_enable\n" // Try fallback approaches
        
        "already_enabled:\n"
        // MMU was already enabled somehow
        "mov w27, #'A'\n"            // 'A' = Already enabled
        "str w27, [x26]\n"
        "mov w27, #'L'\n"            // 'L' = aLready
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = alReady
        "str w27, [x26]\n"
        "b mmu_verify_success\n"     // Jump to verification success path
        
        "mmu_verify_success:\n"
        // Assembly context: MMU enable instruction completed successfully!
        "mov w27, #'M'\n"            // 'M' = MMU
        "str w27, [x26]\n"
        "mov w27, #'M'\n"            // 'M' = mmU
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = mmU  
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = Ok
        "str w27, [x26]\n"
        "mov w27, #'K'\n"            // 'K' = oK → "MMUOK"
        "str w27, [x26]\n"
        
        // After diagnostic dump, proceed to fallback (if needed)
        "b test_progressive_enable\n" // Try progressive/fallback enable
        
        // Re-enable interrupts now that MMU is stable
        "mov w27, #'R'\n"            // 'R' = Re-enable interrupts marker
        "str w27, [x26]\n"
        "mov w27, #'E'\n"            // 'E' = rE-enable
        "str w27, [x26]\n"
        
        "msr daifclr, #15\n"         // Re-enable ALL interrupts (clear D,A,I,F bits)
        "isb\n"                      // Synchronize interrupt state change
        
        // Add a small delay to let MMU stabilize
        "mov x30, #1000\n"           // Small delay counter
        "1:\n"
        "subs x30, x30, #1\n"
        "bne 1b\n"
        
        // Post-MMU instruction pipeline synchronization
        "isb\n"                      // Force instruction pipeline flush
        "nop\n"                      // Pipeline bubble 1
        "nop\n"                      // Pipeline bubble 2
        "dsb sy\n"                   // System-wide data synchronization
        "isb\n"                      // Second instruction synchronization
        
        // Success - MMU is now enabled
        "mov w27, #'S'\n"            // 'S' = Success
        "str w27, [x26]\n"
        "mov w27, #'U'\n"            // 'U' = sUccess
        "str w27, [x26]\n"
        "mov w27, #'C'\n"            // 'C' = suCcess
        "str w27, [x26]\n"
        "mov w27, #'C'\n"            // 'C' = sucCess
        "str w27, [x26]\n"
        
        // Jump directly to continuation point
        "br x22\n"                   // Branch to physical continuation address
        
        "test_progressive_enable:\n"
        // Minimal test failed, try progressive enable approach
        "mov w27, #'P'\n"            // 'P' = Progressive
        "str w27, [x26]\n"
        "mov w27, #'R'\n"            // 'R' = pRogressive
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = prOgressive
        "str w27, [x26]\n"
        
        // Fall through to existing complex sequence as fallback
        
        // STAGE 4B: **EXISTING COMPLEX MMU ENABLE SEQUENCE** (Fallback)
        // If minimal configuration failed, try the original complex sequence
        "mov w27, #'4'\n"
        "str w27, [x26]\n"
        "mov w27, #'B'\n"            // 'B' = Fallback sequence
        "str w27, [x26]\n"
        
        // DEAD CODE: x23 reference (COMMENTED OUT - Step 1)
        // "mov x29, x23\n"             // Save current SCTLR value
        // NOTE: x23 no longer computed (dead code path)
        // Using x28 minimal path instead
        
        // PRE-MMU STATUS CHECK
        "mov w27, #'S'\n"            // 'S' = About to enable MMU
        "str w27, [x26]\n"

        // DEAD CODE: x23 debug output (COMMENTED OUT - Step 1)
        // "mov w27, #'V'\n"            // 'V' = Value
        // "str w27, [x26]\n"
        // "mov w27, #':'\n"
        // "str w27, [x26]\n"
        // // Output x23 value (SCTLR) - show low 16 bits
        // "and x29, x23, #0xFFFF\n"    
        // "lsr x29, x29, #12\n"        // Show high 4 bits of low 16
        // "and x29, x29, #0xF\n"       
        // "cmp x29, #10\n"
        // "b.lt 60f\n"
        // "add x29, x29, #'A'-10\n"
        // "b 61f\n"
        // "60:\n"
        // "add x29, x29, #'0'\n"
        // "61:\n"
        // "str w29, [x26]\n"
        
        // DEAD CODE: x24 test (COMMENTED OUT - Step 1)
        // "mov w27, #'T'\n"            // 'T' = Test minimal
        // "str w27, [x26]\n"
        // "mrs x24, sctlr_el1\n"       // Read firmware SCTLR to preserve RES1 bits
        // "orr x24, x24, #0x1\n"       // Add MMU enable bit
        // "msr sctlr_el1, x24\n"       // Try minimal MMU enable (RES1-safe)
        // "mov w27, #'1'\n"            // '1' = Minimal test completed
        // "str w27, [x26]\n"
        
        // STEP 3A: Verify page table integrity right before MMU
        // STEP 1: Re-seed TTBR0 base register
        "mov x19, %0\n"              // Reload TTBR0 base from input operand
        "ldr x28, [x19]\n"           // Read first L0 entry (now safe!)
        "tst x28, #1\n"              // Check valid bit
        "b.eq page_table_corrupt\n"
        
        // Verify TTBR0 points to our L0 table
        "mrs x28, ttbr0_el1\n"
        "cmp x28, x19\n"
        "b.ne ttbr_mismatch\n"
        
        // DEAD CODE: x23 write attempt (COMMENTED OUT - Step 1)
        // "msr sctlr_el1, x23\n"       // Try MMU enable
        
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
        
        // DEAD CODE: x23 write attempt (COMMENTED OUT - Step 1)
        // "msr sctlr_el1, x23\n"       // Try MMU enable
        
        "wrong_el:\n"
        "mov w27, #'E'\n"            // E = Wrong exception level
        "str w27, [x26]\n"
        
        // STEP 4A: QEMU-specific MMU enable synchronization
        "dsb sy\n"                   // Full system barrier
        "isb\n"                      // Instruction synchronization
        
        // DEAD CODE: Step 2A progressive enable (COMMENTED OUT - Step 1)
        // "mov w27, #'2'\n"            // '2' = Trying Step 2A
        // "str w27, [x26]\n"
        // "mov w27, #'A'\n"            // 'A' = Step 2A
        // "str w27, [x26]\n"
        // 
        // "mrs x24, sctlr_el1\n"       // Read firmware SCTLR to preserve RES1 bits
        // "orr x24, x24, #0x1\n"       // Add M bit only
        // "msr sctlr_el1, x24\n"       // Try MMU-only enable (RES1-safe)
        // "mov w27, #'m'\n"            // 'm' = MMU-only completed
        // "str w27, [x26]\n"
        // 
        // "orr x24, x24, #0x1000\n"    // Add I bit (instruction cache)
        // "msr sctlr_el1, x24\n"       // Try MMU + I
        // "mov w27, #'i'\n"            // 'i' = +instruction cache
        // "str w27, [x26]\n"
        // 
        // "orr x24, x24, #0x4\n"       // Add C bit (data cache)
        // "msr sctlr_el1, x24\n"       // Try MMU + I + C
        // "mov w27, #'c'\n"            // 'c' = +data cache
        // "str w27, [x26]\n"
        
        // DEAD CODE: Step 2B Linux standard values (COMMENTED OUT - Step 1)
        // "mov w27, #'2'\n"            // '2' = Trying Step 2B
        // "str w27, [x26]\n"
        // "mov w27, #'B'\n"            // 'B' = Step 2B
        // "str w27, [x26]\n"
        // 
        // "mrs x24, sctlr_el1\n"       // Read firmware SCTLR to preserve RES1 bits
        // "orr x24, x24, #0x1\n"       // Add MMU enable bit (M=1)
        // "orr x24, x24, #0x4\n"       // Add data cache bit (C=1)
        // "orr x24, x24, #0x1000\n"    // Add instruction cache bit (I=1)
        // "msr sctlr_el1, x24\n"       // Try with standard cache config (RES1-safe)
        // "mov w27, #'L'\n"            // 'L' = Linux values completed
        // "str w27, [x26]\n"
        
        // DEAD CODE: x23 final fallback (COMMENTED OUT - Step 1)
        // "msr sctlr_el1, x23\n"       // ← ORIGINAL MMU Enable (final fallback)
        
        // POST-MMU STATUS CHECK  
        "mov w27, #'M'\n"            // 'M' = MMU enable instruction completed
        "str w27, [x26]\n"
        
        // If we reach here, MMU enable succeeded!
        
        // DEBUG 6: Post-MMU Immediate Test
        "nop\n"                      // Single pipeline bubble
        "mov w27, #'I'\n"            // Immediate test - does this work?
        "str w27, [x26]\n"           // If this fails, MMU enable faulted
        
        /* COMMENTED OUT: Replaced by policy layer post-enable barriers
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
        */
        
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
        
        : // No outputs from third assembly block
        : "r"(page_table_phys_ttbr0), // %0: page table base for TTBR0_EL1
          "r"(page_table_phys_ttbr1), // %1: page table base for TTBR1_EL1
          "r"(tcr),                  // %2: TCR_EL1 value
          "r"(mair),                 // %3: MAIR_EL1 value  
          "r"(continuation_phys),    // %4: continuation point (physical)
          "r"(continuation_virt),    // %5: continuation point (virtual)
          [page_mask] "r"(~0xFFFUL)  // %6: page mask for alignment
        : "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x0", "x1", "x2", "x3", "memory"
    );
    
    // POLICY LAYER: Post-enable barrier sequence (in case execution returns here)
    mmu_barrier_sequence_post_enable();
    
    // We should never reach here
    *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'R'; *uart = 'E'; *uart = 'T'; *uart = 'U'; *uart = 'R'; *uart = 'N';
    *uart = '\r'; *uart = '\n';

#endif // TEST_POLICY_APPROACH
}

/**
 * @brief Post-MMU continuation point running in TTBR1 high virtual space
 * 
 * This function is called after MMU enable from the trampoline.
 * It runs in high virtual address space and finalizes the MMU transition.
 */
__attribute__((noinline))
void mmu_trampoline_continuation_point(void) {
    // CRITICAL: Use HIGH virtual UART address (TTBR1) instead of identity-mapped (TTBR0)
    // This bypasses the UART identity mapping issue and tests TTBR1 UART mapping
    volatile uint32_t* uart = (volatile uint32_t*)UART_VIRT;  // 0xFFFF800009000000
    
    // Debug marker: We're in the continuation point
    *uart = 'C'; *uart = 'O'; *uart = 'N'; *uart = 'T'; *uart = ':'; *uart = 'H'; *uart = 'I'; *uart = 'G'; *uart = 'H';
    *uart = '\r'; *uart = '\n';
    
    // STEP 1 PART 2: Update VBAR to high virtual address (complete Option D!)
    // Read current VBAR (should be physical/low address)
    uint64_t vbar_low;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar_low));
    
    // Calculate high virtual address using bitwise OR
    uint64_t vbar_high = HIGH_VIRT_BASE | (vbar_low & ~0x7FFUL);
    
    // STEP 5: Sanity check alignment
    if ((vbar_high & 0x7FF) != 0) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = ':'; *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = '_'; *uart = 'A'; *uart = 'L';
        *uart = '\r'; *uart = '\n';
        while(1);  // Halt on alignment error
    }
    
    // Update VBAR to high virtual address
    asm volatile("msr vbar_el1, %0\n\tisb" :: "r"(vbar_high));
    
    // STEP 4: Log second VBAR write (skip uart_hex64_early - it uses UART_PHYS)
    *uart = 'V'; *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':';
    *uart = 'H'; *uart = 'I'; *uart = 'G'; *uart = 'H';
    *uart = '\r'; *uart = '\n';
    
    // ✅ CRITICAL: Switch TCR from bootstrap-dual mode to kernel-only mode
    // Now that we're running in high VA (TTBR1 space), we can safely disable TTBR0
    // This sets EPD0=1 (disable TTBR0), EPD1=0 (enable TTBR1)
    // Uses full TCR reconfiguration for architectural safety
    mmu_configure_tcr_kernel_only(VA_BITS_48 ? 48 : 39);
    
    *uart = 'C'; *uart = 'O'; *uart = 'N'; *uart = 'T'; *uart = ':'; *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = '+';
    *uart = '\r'; *uart = '\n';
    
    // STEP 5: Validation test - trigger sync exception to verify vectors work in TTBR1
    *uart = 'T'; *uart = 'E'; *uart = 'S'; *uart = 'T'; *uart = ':'; *uart = 'B'; *uart = 'R'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Deliberately trigger breakpoint - should see sync handler banner
    asm volatile("brk #0");
    
    // If we return from the exception, vectors are working!
    *uart = 'T'; *uart = 'E'; *uart = 'S'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Optional: Switch UART to virtual mapping in the future
    // This would require updating all UART access to use virtual addresses
    // uart_set_base(UART_VIRT);
    
    *uart = 'C'; *uart = 'O'; *uart = 'N'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // MMU transition is now complete
    // Return to caller will continue normal boot flow in high virtual space
} 