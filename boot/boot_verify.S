.section .text
.global boot_state_verify
.global final_verification

// Boot state verification and system integrity checking
// Migrated verification code with enhanced security checks

// Include debug configuration
#include "../include/debug_config.h"

// UART delay macro
.macro uart_delay
    mov x15, #0x8000        
1:  subs x15, x15, #1
    bne 1b
.endm

//==============================================================================
// Function: boot_state_verify
// Description: Comprehensive boot state verification with stack alignment
// Migrated from: start.S lines 158-213 (stack alignment and verification)
// Parameters: x20 = UART base address, sp = current stack pointer
//==============================================================================
boot_state_verify:
    // Save link register and working registers
    stp x30, x19, [sp, #-48]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    mov x19, x20            // Save UART base
    
    // Print stack pointer value before alignment check
    mov w2, #'S'           // Stack pointer marker  
    str w2, [x19]
    uart_delay
    
    // NOTE: These uart_puthex calls are SAFE because they occur AFTER BSS initialization
    // (BSS was zeroed earlier). C functions can be safely called after BSS is properly initialized.
    mov x0, sp             // Pass stack pointer as argument
    bl uart_puthex         // Print stack pointer value
    mov x1, x19            // Restore UART base after function call

    // Enhanced: Verify stack is in expected range
    mov x21, sp
    mov x22, #0x40000000   // Expected minimum stack address
    mov x23, #0x50000000   // Expected maximum stack address
    cmp x21, x22
    blo stack_range_error
    cmp x21, x23
    bhi stack_range_error
    
    // Stack range OK
    mov w2, #'R'           // Range OK
    str w2, [x19]
    uart_delay

    // Fix stack alignment explicitly
    mov w2, #'F'           // Fix alignment marker
    str w2, [x19]
    uart_delay
    
    // Get misalignment value and print it
    mov x3, sp             // Copy SP to x3
    and x4, x3, #0xF       // Get misalignment (bottom 4 bits)
    add w2, w4, #'0'       // Convert to ASCII
    str w2, [x19]          // Print misalignment value
    uart_delay
    
    // Force 16-byte alignment
    bic x3, x3, #0xF       // Clear bottom 4 bits (BIC = bit clear)
    mov sp, x3             // Update SP with aligned value
    
    // Print new stack pointer after adjustment
    mov w2, #'N'           // New stack pointer marker
    str w2, [x19]
    uart_delay
    mov x0, sp             // Pass stack pointer as argument
    bl uart_puthex         // Print stack pointer value
    mov x1, x19            // Restore UART base after function call
    
    // Double-check alignment and print the result
    mov x3, sp             // Copy SP to x3
    and x4, x3, #0xF       // Get bottom 4 bits
    add w2, w4, #'0'       // Convert to ASCII (should print '0' if aligned)
    str w2, [x19]          // Show alignment result
    uart_delay
    
    // Enhanced: Verify alignment was successful
    cbz x4, alignment_success
    
    // Alignment failed
    mov w2, #'!'           // Alignment failure marker
    str w2, [x19]
    uart_delay
    b boot_verify_complete

alignment_success:
    // Alignment successful
    mov w2, #'='           // Alignment success marker
    str w2, [x19]
    uart_delay
    b boot_verify_complete

stack_range_error:
    // Stack pointer outside expected range
    mov w2, #'E'           // Error
    str w2, [x19]
    mov w2, #'R'           // Range
    str w2, [x19]
    uart_delay

boot_verify_complete:
    // Boot state verification complete - configurable output
#ifdef DEBUG_MARKERS_CD_ENABLED
    mov w2, #'C'           // Complete
    str w2, [x19]
    uart_delay
    mov w2, #'D'           // Debug marker
    str w2, [x19]
    uart_delay
#endif
    
    // Restore registers and return
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x30, x19, [sp], #48
    ret

//==============================================================================
// Function: final_verification
// Description: Final system verification before kernel transition
// Migrated from: start.S lines 269-272 with enhancements
// Parameters: x20 = UART base address
//==============================================================================
final_verification:
    // Save link register and working register
    stp x30, x19, [sp, #-16]!
    mov x19, x20            // Save UART base
    
    // Enhanced: Verify critical system state before kernel transition
    
    // Check that we're still at EL1
    mrs x0, CurrentEL
    and x0, x0, #0xC        // Extract EL bits
    cmp x0, #0x4            // Should be 0x4 for EL1
    bne el_verification_error
    
    // EL verification OK
    mov w2, #'E'            // EL
    str w2, [x19]
    mov w2, #'1'            // Level 1
    str w2, [x19]
    uart_delay
    
    // Check MMU is enabled
    mrs x0, sctlr_el1
    and x0, x0, #0x1        // Check M bit (MMU enable)
    cbz x0, mmu_verification_error
    
    // MMU verification OK
    mov w2, #'M'            // MMU
    str w2, [x19]
    mov w2, #'+'            // Enabled
    str w2, [x19]
    uart_delay
    
    // Final success marker
    mov w2, #'2'           // If we return, this confirms no crash (original)
    str w2, [x19]
    uart_delay
    
    b final_verify_complete

el_verification_error:
    mov w2, #'E'            // Error
    str w2, [x19]
    mov w2, #'L'            // Exception Level
    str w2, [x19]
    uart_delay
    b final_verify_complete

mmu_verification_error:
    mov w2, #'E'            // Error
    str w2, [x19]
    mov w2, #'M'            // MMU
    str w2, [x19]
    uart_delay

final_verify_complete:
    // Restore registers and return
    ldp x30, x19, [sp], #16
    ret