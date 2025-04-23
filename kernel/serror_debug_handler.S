// SError debug handler for CustomOS
// This handler is invoked when a System Error exception occurs
// It outputs detailed diagnostic information and halts the system

.section .text
.align 2

// Helper function to output a hexadecimal value
output_hex_value:
    // x0 contains value to print
    // x5 contains UART address (should be set before calling)
    mov x4, #60            // Start with highest nibble (60 = 15*4)
    
hex_print_loop:
    mov x3, x0             // Copy value
    lsr x3, x3, x4         // Shift to get current nibble
    and x3, x3, #0xF       // Mask to get only the nibble
    
    // Convert to ASCII
    cmp x3, #10
    blt 1f
    add x3, x3, #('A' - 10) // For A-F
    b 2f
1:
    add x3, x3, #'0'        // For 0-9
2:
    str w3, [x5]           // Output the character
    
    sub x4, x4, #4         // Move to next nibble
    cmp x4, #0             // Check if done
    bge hex_print_loop     // Continue if not done
    
    // Also print the last nibble (bits 0-3)
    and x3, x0, #0xF
    cmp x3, #10
    blt 3f
    add x3, x3, #('A' - 10)
    b 4f
3:
    add x3, x3, #'0'
4:
    str w3, [x5]          // Output the character
    ret

// SError debug handler with comprehensive information output
.global serror_debug_handler
serror_debug_handler:
    // Save temporary registers
    sub sp, sp, #128
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x29, x30, [sp, #80]
    
    // Setup direct UART output (faster than function calls)
    ldr x5, =0x09000000    // UART data register address
    
    // Output SError banner with distinctive pattern
    mov w0, #'!'
    str w0, [x5]
    mov w0, #'!'
    str w0, [x5]
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'r'
    str w0, [x5]
    mov w0, #'r'
    str w0, [x5]
    mov w0, #'o'
    str w0, [x5]
    mov w0, #'r'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'x'
    str w0, [x5]
    mov w0, #'c'
    str w0, [x5]
    mov w0, #'e'
    str w0, [x5]
    mov w0, #'p'
    str w0, [x5]
    mov w0, #'t'
    str w0, [x5]
    mov w0, #'i'
    str w0, [x5]
    mov w0, #'o'
    str w0, [x5]
    mov w0, #'n'
    str w0, [x5]
    mov w0, #'!'
    str w0, [x5]
    mov w0, #'!'
    str w0, [x5]
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Get current Exception Level
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3
    
    // Print current Exception Level
    mov w1, #'E'
    str w1, [x5]
    mov w1, #'L'
    str w1, [x5]
    mov w1, #':'
    str w1, [x5]
    add w0, w0, #'0'
    str w0, [x5]
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output ELR_EL1 value (return address)
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'R'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'1'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mrs x0, elr_el1
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output ESR_EL1 (Exception Syndrome Register)
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'R'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'1'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mrs x0, esr_el1
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output FAR_EL1 (Fault Address Register)
    mov w0, #'F'
    str w0, [x5]
    mov w0, #'A'
    str w0, [x5]
    mov w0, #'R'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'1'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mrs x0, far_el1
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output SPSR_EL1 (Saved Program Status Register)
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'P'
    str w0, [x5]
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'R'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'1'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mrs x0, spsr_el1
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output SP_EL0 (Stack Pointer EL0)
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'P'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'0'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    mrs x0, sp_el0
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Output SP_EL1 (Current Stack Pointer)
    mov w0, #'S'
    str w0, [x5]
    mov w0, #'P'
    str w0, [x5]
    mov w0, #'_'
    str w0, [x5]
    mov w0, #'E'
    str w0, [x5]
    mov w0, #'L'
    str w0, [x5]
    mov w0, #'1'
    str w0, [x5]
    mov w0, #':'
    str w0, [x5]
    mov w0, #' '
    str w0, [x5]
    // Since we're running in EL1, current SP is SP_EL1
    // We add the size of our saved registers to get the original SP
    add x0, sp, #128
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Extract and display ESR_EL1 components
    mrs x0, esr_el1
    
    // Display EC (Exception Class) - bits [31:26]
    mov w1, #'E'
    str w1, [x5]
    mov w1, #'C'
    str w1, [x5]
    mov w1, #':'
    str w1, [x5]
    mov w1, #' '
    str w1, [x5]
    
    lsr x1, x0, #26
    and x1, x1, #0x3F
    mov x0, x1
    bl output_hex_value
    mov w0, #'\r'
    str w0, [x5]
    mov w0, #'\n'
    str w0, [x5]
    
    // Continuous loop to output all diagnostics repeatedly
    // This ensures the information is visible even if UART buffer is limited
    b serror_debug_handler  // Loop forever, continuously outputting the diagnostics 