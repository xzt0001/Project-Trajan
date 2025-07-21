#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"
#include "../include/memory_config.h"
#include "../include/memory_core.h"

// External global variables from vmm.c
extern uint64_t* l0_table;
extern uint64_t* l0_table_ttbr1;
extern uint64_t saved_vector_table_addr;

// Forward declarations for functions used by enable_mmu_enhanced()
extern void mmu_continuation_point(void);
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
extern void verify_critical_mappings_before_mmu(uint64_t* page_table_base);
extern void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
                      uint64_t phys_start, uint64_t flags);
extern void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, 
                            uint64_t flags, const char* name);
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

// Enhanced function to enable the MMU with improved robustness
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
        
        // Show key interrupt bits: D(3), A(2), I(1), F(0)
        "and x29, x28, #0x8\n"       // Extract D bit (debug exceptions)
        "lsr x29, x29, #3\n"         // Shift to bit 0
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        "and x29, x28, #0x4\n"       // Extract A bit (async aborts)
        "lsr x29, x29, #2\n"         // Shift to bit 0
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        "and x29, x28, #0x2\n"       // Extract I bit (IRQ)
        "lsr x29, x29, #1\n"         // Shift to bit 0
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        "and x29, x28, #0x1\n"       // Extract F bit (FIQ)
        "add w27, w29, #'0'\n"       // Convert to ASCII
        "str w27, [x26]\n"
        
        // STEP 4A: **MINIMAL SCTLR_EL1 CONFIGURATION TEST** (Option 4A)
        // Test with absolute minimum SCTLR_EL1 settings first
        "mov w27, #'4'\n"
        "str w27, [x26]\n"
        "mov w27, #'A'\n"
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Read current SCTLR_EL1 to preserve non-MMU bits
        "mrs x28, sctlr_el1\n"
        
        // Create minimal configuration: preserve all bits except MMU-related ones
        "bic x28, x28, #0x1\n"       // Clear M bit (MMU enable)
        "bic x28, x28, #0x4\n"       // Clear C bit (data cache)
        "bic x28, x28, #0x1000\n"    // Clear I bit (instruction cache)
        
        // Set ONLY the MMU enable bit
        "orr x28, x28, #0x1\n"       // Set M bit (MMU enable) - this is all we need
        
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
        
        // CRITICAL: Comprehensive cache and TLB maintenance before minimal test
        "dsb sy\n"                   // Full system data synchronization
        "tlbi vmalle1\n"             // Invalidate all TLB entries
        "dsb ish\n"                  // Data synchronization barrier (inner shareable)
        "ic iallu\n"                 // Invalidate instruction cache
        "dsb sy\n"                   // Wait for instruction cache invalidation
        "isb\n"                      // Instruction synchronization barrier
        
        // TEST 1: Try minimal MMU enable (MMU bit only, no caches)
        "mov w27, #'T'\n"            // 'T' = Test
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = Test 1
        "str w27, [x26]\n"
        "mov w27, #':'\n"
        "str w27, [x26]\n"
        
        // Attempt minimal MMU enable
        "msr sctlr_el1, x28\n"       // Set minimal SCTLR (just MMU bit)
        "isb\n"                      // Immediate instruction synchronization
        
        // Test if minimal MMU enable worked
        "mrs x29, sctlr_el1\n"       // Read back SCTLR_EL1
        "and x30, x29, #0x1\n"       // Check if MMU bit is set
        "cbnz x30, minimal_mmu_success\n" // Branch if MMU bit is set
        
        // Minimal MMU test failed
        "mov w27, #'F'\n"            // 'F' = Failed
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = Test 1 failed
        "str w27, [x26]\n"
        "b test_progressive_enable\n" // Try progressive enable
        
        "minimal_mmu_success:\n"
        // Minimal MMU enable succeeded!
        "mov w27, #'S'\n"            // 'S' = Success  
        "str w27, [x26]\n"
        "mov w27, #'1'\n"            // '1' = Test 1 succeeded
        "str w27, [x26]\n"
        
        // Add a small delay to let MMU stabilize
        "mov x30, #1000\n"           // Small delay counter
        "1:\n"
        "subs x30, x30, #1\n"
        "bne 1b\n"
        
        // Post-MMU instruction pipeline synchronization for minimal config
        "isb\n"                      // Force instruction pipeline flush
        "nop\n"                      // Pipeline bubble 1
        "nop\n"                      // Pipeline bubble 2
        "dsb sy\n"                   // System-wide data synchronization
        "isb\n"                      // Second instruction synchronization
        
        // Success with minimal configuration - now optionally add caches
        "mov w27, #'A'\n"            // 'A' = Add caches
        "str w27, [x26]\n"
        "mov w27, #'D'\n"            // 'D' = aDd
        "str w27, [x26]\n"
        "mov w27, #'D'\n"            // 'D' = aDd
        "str w27, [x26]\n"
        
        // Optionally add instruction cache (safer than data cache)
        "orr x28, x28, #0x1000\n"    // Add I bit (instruction cache)
        "msr sctlr_el1, x28\n"       // Update SCTLR_EL1
        "isb\n"                      // Synchronize
        
        // Test if instruction cache addition worked
        "mrs x29, sctlr_el1\n"       // Read back
        "and x30, x29, #0x1000\n"    // Check I bit
        "cbnz x30, icache_added\n"   // Branch if I bit is set
        
        // I-cache addition failed, but MMU still works
        "mov w27, #'i'\n"            // 'i' = icache failed (lowercase)
        "str w27, [x26]\n"
        "b minimal_mmu_final\n"
        
        "icache_added:\n"
        "mov w27, #'I'\n"            // 'I' = Icache added successfully
        "str w27, [x26]\n"
        
        // Optionally try adding data cache (most risky)
        "orr x28, x28, #0x4\n"       // Add C bit (data cache)
        "msr sctlr_el1, x28\n"       // Update SCTLR_EL1
        "isb\n"                      // Synchronize
        
        // Test if data cache addition worked
        "mrs x29, sctlr_el1\n"       // Read back
        "and x30, x29, #0x4\n"       // Check C bit
        "cbnz x30, dcache_added\n"   // Branch if C bit is set
        
        // D-cache addition failed, but MMU + I-cache still works
        "mov w27, #'c'\n"            // 'c' = dcache failed (lowercase)
        "str w27, [x26]\n"
        "b minimal_mmu_final\n"
        
        "dcache_added:\n"
        "mov w27, #'C'\n"            // 'C' = Dcache added successfully
        "str w27, [x26]\n"
        
        "minimal_mmu_final:\n"
        // Final success with minimal configuration
        "mov w27, #'M'\n"            // 'M' = Minimal
        "str w27, [x26]\n"
        "mov w27, #'O'\n"            // 'O' = Ok
        "str w27, [x26]\n"
        "mov w27, #'K'\n"            // 'K' = oK
        "str w27, [x26]\n"
        
        // Jump directly to continuation point - minimal config worked
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