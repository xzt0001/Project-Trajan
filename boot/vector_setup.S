.section .text
.global vector_table_setup
.extern vector_table

// Vector table setup and security enhancements
// Migrated from start.S lines 214-245 with security improvements

// UART delay macro
.macro uart_delay
    mov x15, #0x8000        
1:  subs x15, x15, #1
    bne 1b
.endm

//==============================================================================
// Function: vector_table_setup
// Description: Configure VBAR_EL1 with enhanced security verification
// Migrated from: start.S lines 214-245
// Parameters: x20 = UART base address (preserved)
//==============================================================================
vector_table_setup:
    // Save link register and working registers
    stp x30, x19, [sp, #-32]!
    stp x21, x22, [sp, #16]
    mov x19, x20            // Save UART base
    
    // About to call init_vmm() - print 'v'
    mov w2, #'v'
    str w2, [x19]
    uart_delay             // Add delay after UART write
    
    // CRITICAL FIX: Set VBAR_EL1 to vector_table directly before MMU initialization
    mov w2, #'V'
    str w2, [x19]
    mov w2, #'B'
    str w2, [x19] 
    mov w2, #':'
    str w2, [x19]
    
    // Load the vector_table address
    ldr x0, =vector_table
    
    // Enhanced: Verify vector table address is valid
    cbz x0, vector_address_error
    
    // Check that vector table is properly aligned (should be 2KB aligned)
    and x21, x0, #0x7FF         // Check bottom 11 bits (2KB = 2^11)
    cbnz x21, vector_alignment_error
    
    // Set VBAR_EL1 to the physical address of vector_table
    msr vbar_el1, x0
    isb                     // Instruction synchronization barrier
    
    // Verify VBAR_EL1 was set correctly
    mrs x21, vbar_el1
    cmp x21, x0
    bne vector_verification_error
    
    // Print first hex digit of VBAR_EL1 (original functionality)
    mov x22, x21            // Save VBAR_EL1 value for printing
    lsr x0, x21, #28
    and x0, x0, #0xF
    cmp x0, #10
    blt 1f
    add w0, w0, #('A' - 10)
    b 2f
1:  add w0, w0, #'0'
2:  str w0, [x19]           // Output the digit
    
    mov w2, #':'
    str w2, [x19]
    
    // Enhanced: Print more digits for better verification
    lsr x0, x22, #24
    and x0, x0, #0xF
    cmp x0, #10
    blt 3f
    add w0, w0, #('A' - 10)
    b 4f
3:  add w0, w0, #'0'
4:  str w0, [x19]           // Output second digit
    
    // Success marker
    mov w2, #'+'
    str w2, [x19]
    uart_delay
    
    b vector_setup_complete

vector_address_error:
    // Vector table address is NULL
    mov w2, #'E'            // Error
    str w2, [x19]
    mov w2, #'A'            // Address
    str w2, [x19]
    uart_delay
    b vector_setup_complete

vector_alignment_error:
    // Vector table not properly aligned
    mov w2, #'E'            // Error
    str w2, [x19]
    mov w2, #'L'            // aLignment
    str w2, [x19]
    uart_delay
    b vector_setup_complete

vector_verification_error:
    // VBAR_EL1 verification failed
    mov w2, #'E'            // Error
    str w2, [x19]
    mov w2, #'V'            // Verification
    str w2, [x19]
    uart_delay

vector_setup_complete:
    // Final verification marker
    mov w2, #'K'            // oK marker
    str w2, [x19]
    uart_delay
    
    // Restore registers and return
    ldp x21, x22, [sp, #16]
    ldp x30, x19, [sp], #32
    ret