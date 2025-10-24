/**
 * @file trampoline.S
 * @brief ARM64 MMU Trampoline for Physical→Virtual PC Transition
 * 
 * This assembly file provides atomic transition from TTBR0 (physical) 
 * to TTBR1 (high virtual) address space during MMU enable sequence.
 * 
 * Key Features:
 * - Dual-mapped at both low and high virtual addresses
 * - 2KB-aligned section for optimal L3 PTE coverage
 * - Atomic PC transition without instruction fetch faults
 */

#include "../include/memory_config.h"

.section .text.tramp, "ax"
.align 11  // 2KB alignment (2^11 = 2048 bytes)

// Global symbols for linker and C code access
.global mmu_trampoline_low
.global mmu_trampoline_high  
.global _trampoline_section_start
.global _trampoline_section_end

_trampoline_section_start:

/**
 * @brief Low virtual address trampoline entry point
 * Entry: Called from TTBR0 space (current PC context, MMU disabled)
 * Actions: Enables MMU at identity-mapped address, then jumps to TTBR1 space
 * Exit: Branches to mmu_trampoline_high in TTBR1 address space
 */
mmu_trampoline_low:
    // Save critical registers
    stp x29, x30, [sp, #-16]!   // Save frame pointer and link register
    stp x19, x20, [sp, #-16]!   // Save callee-saved registers
    stp x21, x22, [sp, #-16]!
    
    // Debug marker: Show we're in low trampoline (before MMU enable)
    mov x19, #0x09000000        // UART base (physical address)
    mov w20, #'T'
    str w20, [x19]
    mov w20, #'L'
    str w20, [x19]
    mov w20, #'O'
    str w20, [x19]
    mov w20, #'W'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // ============================================================
    // CRITICAL: Enable MMU while at identity-mapped address
    // This must happen BEFORE jumping to high virtual address
    // ============================================================
    
    // Step 1: Configure TCR_EL1 for dual-table bootstrap mode
    mrs  x0, tcr_el1
    bic  x0, x0, #(1 << 7)      // Clear EPD0 (enable TTBR0 walks)
    bic  x0, x0, #(1 << 23)     // Clear EPD1 (enable TTBR1 walks)
    msr  tcr_el1, x0
    isb                          // Synchronize instruction stream
    
    // CRITICAL: Ensure TCR write is visible to MMU hardware before enabling
    dsb  sy                      // Data synchronization barrier (system-wide)
    isb                          // Extra instruction barrier for safety
    
    // Step 2: Enable MMU by setting M bit in SCTLR_EL1
    mrs  x0, sctlr_el1
    orr  x0, x0, #0x1           // Set M bit (MMU enable)
    msr  sctlr_el1, x0
    isb                          // Mandatory synchronization after MMU enable
    
    // Step 3: Verify MMU is enabled (defensive check)
    mrs  x0, sctlr_el1
    and  x0, x0, #0x1
    cbnz x0, 1f                 // Branch if MMU enabled (expected path)
    
    // MMU enable failed - print failure marker and halt
    mov w20, #'M'
    str w20, [x19]
    mov w20, #'F'
    str w20, [x19]
    mov w20, #'A'
    str w20, [x19]
    mov w20, #'I'
    str w20, [x19]
    mov w20, #'L'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    b .                          // Infinite loop on failure
    
1:  // MMU enabled successfully
    
    // Step 4: Print MMU enable success marker
    mov w20, #'M'
    str w20, [x19]
    mov w20, #'+'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // ============================================================
    // Now safe to jump to high virtual address (MMU is enabled)
    // ============================================================
    
    // Load HIGH_VIRT_BASE constant
#if VA_BITS_48
    mov x21, #0x8000            // HIGH_VIRT_BASE = 0xFFFF800000000000
    movk x21, #0xFFFF, lsl #48  // Set upper 16 bits
#else  
    mov x21, #0x8000            // HIGH_VIRT_BASE = 0xFFFFFF8000000000  
    movk x21, #0xFFFF, lsl #48  // Set bits 48-63
    movk x21, #0xFFFF, lsl #32  // Set bits 32-47
#endif
    
    // Calculate high virtual address of trampoline_high
    adr x20, mmu_trampoline_high    // Get physical address of high trampoline
    orr x20, x20, x21               // Add HIGH_VIRT_BASE to create high virtual address
    
    // Atomic jump to high virtual space (MMU enabled, translation works)
    br x20                          // Branch to TTBR1 equivalent

/**
 * @brief High virtual address trampoline entry point  
 * Entry: Jumped to from TTBR0→TTBR1 transition (MMU already enabled)
 * Exit: Calls continuation point in high virtual space
 */
mmu_trampoline_high:
    // Debug marker: Show we reached high virtual address space
    mov x19, #0x09000000        // UART base (identity-mapped, accessible)
    mov w20, #'T'
    str w20, [x19]
    mov w20, #'H'
    str w20, [x19]
    mov w20, #'I'
    str w20, [x19]
    mov w20, #'G'
    str w20, [x19]
    mov w20, #'H'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // Continue to post-MMU C code (now safe - MMU is enabled, we're in TTBR1)
    bl mmu_trampoline_continuation_point
    
    // Restore registers and return
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret

_trampoline_section_end:

// Export section size for C code in regular data section
.section .data
.align 3
.global _trampoline_section_size
_trampoline_section_size:
    .quad (_trampoline_section_end - _trampoline_section_start)
