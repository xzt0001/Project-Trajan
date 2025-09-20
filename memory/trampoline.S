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
 * Entry: Called from TTBR0 space (current PC context)
 * Exit: Jumps to TTBR1 space for MMU enable
 */
mmu_trampoline_low:
    // Save critical registers
    stp x29, x30, [sp, #-16]!   // Save frame pointer and link register
    stp x19, x20, [sp, #-16]!   // Save callee-saved registers
    stp x21, x22, [sp, #-16]!
    
    // Load HIGH_VIRT_BASE constant
#if VA_BITS_48
    mov x21, #0x8000            // HIGH_VIRT_BASE = 0xFFFF800000000000
    movk x21, #0xFFFF, lsl #48  // Set upper 16 bits
#else  
    mov x21, #0x8000            // HIGH_VIRT_BASE = 0xFFFFFF8000000000  
    movk x21, #0xFFFF, lsl #48  // Set bits 48-63
    movk x21, #0xFFFF, lsl #32  // Set bits 32-47
#endif

    // Debug marker: Show we're in low trampoline
    mov x19, #0x09000000        // UART base
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
    
    // Calculate high virtual address of trampoline_high
    adr x20, mmu_trampoline_high    // Get physical address of high trampoline
    orr x20, x20, x21               // Add HIGH_VIRT_BASE to create high virtual address
    
    // Atomic jump to high virtual space
    br x20                          // Branch to TTBR1 equivalent

/**
 * @brief High virtual address trampoline entry point  
 * Entry: Jumped to from TTBR0→TTBR1 transition
 * Exit: Calls MMU enable and continues in high virtual space
 */
mmu_trampoline_high:
    // Debug marker: Show we're in high trampoline
    mov x19, #0x09000000        // UART base (still physical until UART remapped)
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
    
    // Set EPD for bootstrap dual-table mode
    bl mmu_policy_set_epd_bootstrap_dual
    
    // Enable MMU translation
    bl mmu_enable_translation
    
    // Debug marker: MMU enabled successfully
    mov x19, #0x09000000        // UART base
    mov w20, #'M'
    str w20, [x19]
    mov w20, #'M'
    str w20, [x19]
    mov w20, #'U'
    str w20, [x19]
    mov w20, #'+'
    str w20, [x19]
    mov w20, #'\r'
    str w20, [x19]
    mov w20, #'\n'
    str w20, [x19]
    
    // Continue to post-MMU C code
    bl mmu_trampoline_continuation_point
    
    // Restore registers and return
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret

_trampoline_section_end:

// Export section size for C code
.global _trampoline_section_size
_trampoline_section_size:
    .quad (_trampoline_section_end - _trampoline_section_start)
