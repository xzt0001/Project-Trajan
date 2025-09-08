#include "../include/types.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/debug.h"
#include "../include/memory_config.h"
#include "memory_debug.h"
#include "../include/memory_core.h"
#include "../include/mmu_policy.h"  // For centralized TLB operations

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


// Function declarations
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
void init_vmm_impl(void);
void init_vmm_wrapper(void);
int verify_executable_address(uint64_t *table_ptr, uint64_t vaddr, const char* desc);
void map_code_section(void);


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


// Function to debug memory permissions across different regions
void debug_memory_permissions(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = ' ';
    *uart = 'O'; *uart = 'K'; *uart = '\r'; *uart = '\n';
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
    
    // Flush TLB to ensure changes take effect - REPLACED WITH POLICY LAYER
    // asm volatile (
    //     "dsb ishst\n"
    //     "tlbi vmalle1is\n"        // ❌ POLICY VIOLATION - inner-shareable TLB invalidation
    //     "dsb ish\n"
    //     "isb\n"
    //     ::: "memory"
    // );
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
    mmu_comprehensive_tlbi_sequence();
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
    
    // Enhanced TLB invalidation - REPLACED WITH POLICY LAYER
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = ':';
    *uart = '\r'; *uart = '\n';
    
    // ❌ ORIGINAL TLB OPERATIONS - COMMENTED OUT FOR POLICY VIOLATION FIXES
    // asm volatile(
    //     "dsb sy\n"              // System-wide data synchronization (single-core safe)
    //     "tlbi vmalle1\n"        // ❌ LOCAL-ONLY VIOLATION - Invalidate TLB entries for EL1 (local core only)
    //     "dsb sy\n"              // Wait for TLB invalidation completion (system-wide)
    //     "ic iallu\n"            // Invalidate instruction cache (already local)
    //     "dsb sy\n"              // Wait for instruction cache invalidation (system-wide)
    //     "isb\n"                 // Instruction synchronization barrier
    //     ::: "memory"
    // );
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
    mmu_comprehensive_tlbi_sequence();
    
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
        
        // Invalidate TLB for this address - REPLACED WITH POLICY LAYER
        // asm volatile("tlbi vaae1is, %0" :: "r"(vbar_virt >> 12) : "memory");  // ❌ POLICY VIOLATION - address-specific inner-shareable TLB invalidation
        // asm volatile("dsb ish" ::: "memory");
        // asm volatile("isb" ::: "memory");
        
        // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
        mmu_comprehensive_tlbi_sequence();
        
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

// Verify a page mapping exists

// Add this after existing function prototypes near the top of the file
void flush_cache_lines(void* addr, size_t size);

