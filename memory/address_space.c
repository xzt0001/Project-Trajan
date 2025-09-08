#include "../include/types.h"
#include "../include/address_space.h"
#include "../include/memory_config.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/memory_core.h"
#include "memory_debug.h"
#include "../include/uart.h"
#include "../include/debug.h"
#include "../include/mmu_policy.h"  // For centralized TLB operations

// Global state tracking
static bool mmu_initialization_attempted = false;
static bool mmu_enabled_successfully = false;
static bool using_bypass_mode = false;

// Helper function declaration
static bool attempt_vmm_initialization_with_timeout(void);

int init_memory_subsystem(void) {
    // Direct UART for granular tracking
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Phase 1: Always initialize PMM first
    *uart = '['; *uart = 'U'; *uart = 'M'; *uart = 'S'; *uart = ']'; // [UMS] - Unified Memory Subsystem entry
    
    *uart = 'P'; *uart = '1'; // P1 - Phase 1 start
    init_pmm();
    *uart = 'P'; *uart = '1'; *uart = 'K'; // P1K - PMM init complete
    
    // Phase 2: Attempt MMU initialization
    if (!mmu_initialization_attempted) {
        *uart = 'P'; *uart = '2'; // P2 - Phase 2 start
        mmu_initialization_attempted = true;
        
        // Try to initialize VMM with timeout/watchdog
        *uart = 'A'; *uart = 'T'; *uart = 'T'; // ATT - Attempting VMM initialization
        if (attempt_vmm_initialization_with_timeout()) {
            *uart = 'M'; *uart = 'S'; *uart = 'U'; // MSU - MMU Success
            mmu_enabled_successfully = true;
            using_bypass_mode = false;
            *uart = 'R'; *uart = '0'; // R0 - Return 0 (full MMU success)
            return 0; // Full MMU success
        } else {
            *uart = 'M'; *uart = 'F'; *uart = 'L'; // MFL - MMU Failed
            // MMU failed, fall back to PMM-only mode
            using_bypass_mode = true;
            *uart = 'B'; *uart = 'Y'; *uart = 'P'; // BYP - Bypass mode
            *uart = 'R'; *uart = '1'; // R1 - Return 1 (PMM-only mode)
            return 1; // PMM-only mode
        }
    }
    
    *uart = 'R'; *uart = 'E'; *uart = 'T'; // RET - Return path
    return using_bypass_mode ? 1 : 0;
}

void* addr_alloc_page(void) {
    // Purpose: Unified page allocation interface
    // Logic: Always uses PMM's alloc_page() regardless of MMU state
    // Return: Physical address pointer
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'A'; *uart = 'P'; // AP - Alloc Page
    
    void* result = alloc_page();
    
    if (result) {
        *uart = 'A'; *uart = 'O'; *uart = 'K'; // AOK - Alloc OK
    } else {
        *uart = 'A'; *uart = 'F'; *uart = 'L'; // AFL - Alloc Failed
    }
    
    return result;
}

int addr_free_page(void* addr) {
    // Purpose: Unified page deallocation
    // Logic: Uses PMM's free_page() with additional validation
    // Return: 0=success, -1=failure
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'F'; *uart = 'P'; // FP - Free Page
    
    if (!addr) {
        *uart = 'F'; *uart = 'N'; *uart = 'L'; // FNL - Free Null
        return -1;
    }
    
    free_page(addr);
    *uart = 'F'; *uart = 'O'; *uart = 'K'; // FOK - Free OK
    return 0;
}

int addr_map_range(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, uint64_t flags) {
    // Purpose: Smart range mapping with MMU bypass
    // Logic: If MMU enabled → use VMM's map_range(), else → identity mapping via PMM
    // Return: 0=success, -1=failure
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'M'; *uart = 'R'; // MR - Map Range
    
    if (!using_bypass_mode && mmu_enabled_successfully) {
        *uart = 'M'; *uart = 'V'; // MV - Map via VMM
        // Use VMM's map_range() function
        uint64_t* l0_table = get_kernel_page_table();
        if (l0_table) {
            *uart = 'M'; *uart = 'G'; // MG - Map Go
            map_range(l0_table, virt_start, virt_end, phys_start, flags);
            *uart = 'M'; *uart = 'K'; // MK - Map OK
            return 0;
        } else {
            *uart = 'M'; *uart = 'E'; // ME - Map Error (no L0 table)
        }
    } else {
        *uart = 'M'; *uart = 'B'; // MB - Map Bypass
    }
    
    // MMU bypass mode - identity mapping is automatic
    // In bypass mode, virtual addresses equal physical addresses
    *uart = 'M'; *uart = 'I'; // MI - Map Identity
    return 0;
}

int addr_map_device(uint64_t phys_addr, uint64_t virt_addr, uint64_t size, uint64_t flags) {
    // Purpose: Device MMIO mapping (UART, timers, etc.)
    // Logic: If MMU enabled → virtual mapping, else → identity mapping
    // Return: 0=success, -1=failure
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'D'; *uart = 'M'; // DM - Device Map
    
    if (!using_bypass_mode && mmu_enabled_successfully) {
        *uart = 'D'; *uart = 'V'; // DV - Device VMM
        // Use VMM for device mapping
        uint64_t* l0_table = get_kernel_page_table();
        if (l0_table) {
            *uart = 'D'; *uart = 'G'; // DG - Device Go
            // Map device with proper device memory attributes
            uint64_t device_flags = flags | PTE_DEVICE_nGnRE | PTE_PXN | PTE_UXN;
            map_range(l0_table, virt_addr, virt_addr + size, phys_addr, device_flags);
            *uart = 'D'; *uart = 'K'; // DK - Device OK
            return 0;
        } else {
            *uart = 'D'; *uart = 'E'; // DE - Device Error (no L0 table)
        }
    } else {
        *uart = 'D'; *uart = 'B'; // DB - Device Bypass
    }
    
    // Bypass mode - identity mapping is automatic
    *uart = 'D'; *uart = 'I'; // DI - Device Identity
    return 0;
}

int addr_unmap_page(uint64_t virt_addr) {
    // Purpose: Unified page unmapping
    // Logic: If MMU enabled → clear PTE and TLB invalidate, else → no-op
    // Return: 0=success, -1=failure
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'U'; *uart = 'M'; // UM - Unmap
    
    if (!using_bypass_mode && mmu_enabled_successfully) {
        *uart = 'U'; *uart = 'V'; // UV - Unmap VMM
        // Get the L3 table for this address
        uint64_t* l0_table = get_kernel_page_table();
        if (l0_table) {
            *uart = 'U'; *uart = 'G'; // UG - Unmap Go
            uint64_t* l3_table = get_l3_table_for_addr(l0_table, virt_addr);
            if (l3_table) {
                *uart = 'U'; *uart = 'C'; // UC - Unmap Clear
                // Clear the L3 entry
                uint64_t l3_idx = (virt_addr >> 12) & 0x1FF;
                l3_table[l3_idx] = 0;
                
                // TLB invalidation - REPLACED WITH POLICY LAYER
                // asm volatile("tlbi vaae1is, %0" :: "r"(virt_addr >> 12) : "memory");  // ❌ POLICY VIOLATION - address-specific inner-shareable TLB invalidation
                // asm volatile("dsb ish" ::: "memory");
                // asm volatile("isb" ::: "memory");
                
                // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
                mmu_comprehensive_tlbi_sequence();
                
                *uart = 'U'; *uart = 'K'; // UK - Unmap OK
                return 0;
            } else {
                *uart = 'U'; *uart = 'E'; // UE - Unmap Error (no L3 table)
            }
        } else {
            *uart = 'U'; *uart = 'L'; // UL - Unmap Error (no L0 table)
        }
        return -1;
    }
    
    // Bypass mode - no-op
    *uart = 'U'; *uart = 'B'; // UB - Unmap Bypass
    return 0;
}

// Memory subsystem status
bool is_mmu_enabled(void) {
    return mmu_enabled_successfully;
}

bool is_virtual_addressing_available(void) {
    return !using_bypass_mode;
}

// Helper function to attempt VMM initialization with timeout
static bool attempt_vmm_initialization_with_timeout(void) {
    // Try to initialize VMM with timeout/watchdog
    // If VMM hangs, fall back to PMM-only mode
    
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    *uart = 'V'; *uart = 'T'; *uart = 'R'; // VTR - VMM Try
    
    // In a real implementation, this would have a watchdog timer
    // For now, we'll try the VMM initialization directly
    
    // Check if VMM functions are available
    *uart = 'V'; *uart = 'C'; *uart = 'K'; // VCK - VMM Check
    if (get_kernel_page_table == NULL) {
        *uart = 'V'; *uart = 'N'; *uart = 'A'; // VNA - VMM Not Available
        return false;
    }
    
    // Try to initialize VMM
    *uart = 'V'; *uart = 'I'; *uart = 'N'; // VIN - VMM Init
    
    // Add a simple marker before calling init_vmm
    *uart = 'V'; *uart = 'G'; *uart = 'O'; // VGO - VMM Go
    
    init_vmm();
    
    // If we get here without hanging, VMM initialization succeeded
    *uart = 'V'; *uart = 'S'; *uart = 'U'; // VSU - VMM Success
    return true;
} 