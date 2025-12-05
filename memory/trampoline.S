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
    // CRITICAL: Enable MMU at identity-mapped address
    // TCR already configured correctly (EPD0=0, EPD1=0) by caller
    // Just enable MMU and jump to high virtual address
    // ============================================================
    
    // Pre-MMU barrier sequence (defensive - ensures all prior ops complete)
    dsb  sy                      // Ensure page tables visible to MMU hardware
    isb                          // Sync context before system register write
    
    // DIAGNOSTIC: Print "E1" - about to read SCTLR
    mov w20, #'E'
    str w20, [x19]
    mov w20, #'1'
    str w20, [x19]
    
    // Enable MMU by setting M bit in SCTLR_EL1
    mrs  x0, sctlr_el1
    
    // DIAGNOSTIC: Print "E2" - SCTLR read succeeded, about to modify
    mov w20, #'E'
    str w20, [x19]
    mov w20, #'2'
    str w20, [x19]
    
    orr  x0, x0, #0x1           // Set M bit (MMU enable)
    
    // DIAGNOSTIC: Print "E3" - about to write SCTLR (MMU ENABLE!)
    mov w20, #'E'
    str w20, [x19]
    mov w20, #'3'
    str w20, [x19]
    
    msr  sctlr_el1, x0          // <<< THE CRITICAL INSTRUCTION
    
    // DIAGNOSTIC: Print "E4" - MSR completed! (if we see this, MMU enable succeeded)
    mov w20, #'E'
    str w20, [x19]
    mov w20, #'4'
    str w20, [x19]
    
    // Post-MMU barrier sequence (mandatory per ARM ARM)
    dsb  sy                      // Ensure SCTLR write completes
    isb                          // Synchronize instruction stream with MMU ON
    
    // DIAGNOSTIC: Print "E5" - barriers completed
    mov w20, #'E'
    str w20, [x19]
    mov w20, #'5'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // Verify MMU is enabled (defensive check)
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
    
    // ============================================================
    // DIAGNOSTIC: Print "M+" to confirm MMU enable verification passed
    // x19 still holds UART address (0x09000000), identity-mapped in TTBR0
    // ============================================================
    mov w20, #'M'
    str w20, [x19]
    mov w20, #'+'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // Load HIGH_VIRT_BASE constant = 0xFFFF800000000000 (for 48-bit VA)
    // 
    // BUG FIX: Original code placed 0x8000 at bits [15:0] instead of [47:32]
    // This caused branch target 0xFFFF000040091XXX instead of 0xFFFF800040091XXX
    // resulting in L0[0] lookup in TTBR1 (unmapped) instead of L0[256] (mapped)
    //
    // NOTE: Removed #if/#else preprocessor conditionals - assembler doesn't process them!
    // Using 48-bit VA scheme (matches TCR_EL1 T0SZ=16, T1SZ=16 configuration)
    //
    // Correctly construct 0xFFFF800000000000:
    //   Bits [63:48] = 0xFFFF (canonical extension)
    //   Bits [47:32] = 0x8000 (high address marker)
    //   Bits [31:0]  = 0x00000000
    //
    movz x21, #0x8000, lsl #32      // x21 = 0x0000_8000_0000_0000 (0x8000 at bits [47:32])
    movk x21, #0xFFFF, lsl #48      // x21 = 0xFFFF_8000_0000_0000 (HIGH_VIRT_BASE)
    
    // Calculate high virtual address of trampoline_high
    adr x20, mmu_trampoline_high    // Get physical address of high trampoline
    orr x20, x20, x21               // Add HIGH_VIRT_BASE to create high virtual address
    
    // DIAGNOSTIC: Print "BR" before branch to high address
    // Save x20 (branch target) in x22 temporarily
    mov x22, x20
    mov w20, #'B'
    str w20, [x19]
    mov w20, #'R'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    mov x20, x22                    // Restore branch target
    
    // Atomic jump to high virtual space (MMU enabled, translation works)
    br x20                          // Branch to TTBR1 equivalent

/**
 * @brief High virtual address trampoline entry point  
 * Entry: Jumped to from TTBR0→TTBR1 transition (MMU already enabled)
 * Exit: Calls continuation point in high virtual space
 */
mmu_trampoline_high:
    // ============================================================
    // DIAGNOSTIC: Print "HI" to confirm we reached high virtual address
    // At this point, we're executing from TTBR1 space (0xFFFF800040091XXX)
    // UART must be accessed via HIGH virtual address now!
    // ============================================================
    
    // Load HIGH UART virtual address for post-MMU printing
    movz x19, #0x0900, lsl #16      // x19 = 0x0000000009000000
    movk x19, #0x8000, lsl #32      // x19 = 0x0000800009000000
    movk x19, #0xFFFF, lsl #48      // x19 = 0xFFFF800009000000 (HIGH UART)
    
    mov w20, #'H'
    str w20, [x19]
    mov w20, #'I'
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
