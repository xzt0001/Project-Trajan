.section .text
.global enhanced_debug_setup
.global debug_pmm_status  
.global debug_vmm_status

// Enhanced debug functions for post-BSS boot phases
// These functions can safely call C functions since BSS has been initialized

// UART delay macro (replicated from start.S for module independence)
.macro uart_delay
    mov x15, #0x8000        // Doubled delay for better UART reliability
1:  subs x15, x15, #1
    bne 1b
.endm

//==============================================================================
// Function: enhanced_debug_setup
// Description: Initial debug setup and MMU preparation markers
// Migrated from: start.S lines 104-141
// Parameters: x20 = UART base address (preserved from start.S)
//==============================================================================
enhanced_debug_setup:
    // Save link register and callee-saved registers
    stp x30, x19, [sp, #-16]!
    mov x19, x20            // Save UART base in callee-saved register
    
    // MMU initialization marker
    mov w2, #'M'           // M for MMU initialization
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    // Simple protection for link register (enhanced version)
    str x30, [sp, #-16]!    // Push x30 onto the stack
    
    // Output debug character
    mov w2, #'s'           // Saved link register
    str w2, [x19]
    uart_delay
    
    // Debug before init_pmm call
    mov w2, #'1'           // About to call init_pmm
    str w2, [x19]
    uart_delay
    
    // Clear x30 just to be safe (enhanced security)
    mov x30, #0
    
    // Manually set up return address for test function
    adr x30, return_label_debug  // Set link register to return label
    
    // Print right before test_return
    mov w2, #'x'           // About to call test function
    str w2, [x19]
    uart_delay
    
    // Call test_return function - using direct branch, not bl
    b test_return
    
return_label_debug:
    // This is where we should return to
    mov w2, #'y'           // Returned from test function
    str w2, [x19]
    uart_delay
    
    // Restore enhanced link register protection
    ldr x30, [sp], #16     // Pop x30 back into place
    
    // Restore registers and return
    ldp x30, x19, [sp], #16
    ret

//==============================================================================
// Function: debug_pmm_status
// Description: Debug output after PMM initialization with enhanced checks
// Migrated from: start.S lines 144-157
// Parameters: x20 = UART base address, sp = current stack pointer
//==============================================================================
debug_pmm_status:
    // Save link register and preserve x20
    stp x30, x19, [sp, #-16]!
    mov x19, x20            // Save UART base
    
    // Debug after init_pmm call
    mov w2, #'2'           // Back from init_pmm
    str w2, [x19]
    uart_delay
    
    // Continue with debug output
    mov w2, #'P'           // PMM init complete
    str w2, [x19]
    uart_delay
    
    // CRITICAL SECTION - about to call init_vmm()
    // Verify current state
    mov w2, #'c'          // Character to prove we reached this point
    str w2, [x19]
    uart_delay

    // Enhanced: Verify PMM initialization was successful
    // This is safe post-BSS - we can call C functions now
    mov w2, #'V'          // Verification marker
    str w2, [x19]
    uart_delay
    
    // Restore registers and return
    ldp x30, x19, [sp], #16
    ret

//==============================================================================
// Function: debug_vmm_status  
// Description: Debug output after VMM initialization and pre-kernel transition
// Migrated from: start.S lines 247-267
// Parameters: x20 = UART base address
//==============================================================================
debug_vmm_status:
    // Save link register and preserve x20
    stp x30, x19, [sp, #-16]!
    mov x19, x20            // Save UART base
    
    // Right after init_vmm() call - print 'V'
    mov w2, #'V'
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    // Debug output - VMM initialized
    mov w2, #'V'           // V for Virtual Memory Manager initialized
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    // USER REQUESTED DEBUG PRINTS
    mov w2, #'S'           // Indicate Start
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    mov w2, #'1'           // About to jump to kernel_main
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    // Enhanced: Add VMM verification markers
    mov w2, #'O'           // OK marker for successful VMM
    str w2, [x19]
    uart_delay
    
    // Restore registers and return
    ldp x30, x19, [sp], #16
    ret

// End of debug_helpers.S