.section .text.boot
.global _start
.extern kernel_main
.extern _bss_start
.extern _bss_end
.extern vector_table
.extern init_pmm
.extern test_return
.extern init_vmm
.extern get_kernel_page_table
.extern enable_mmu
.extern uart_puthex

// UART delay macro to ensure characters are transmitted
.macro uart_delay
    mov x15, #0x8000        // Doubled delay for better UART reliability
1:  subs x15, x15, #1
    bne 1b
.endm

_start:
    // Save UART address in callee-saved register
    mov x20, #0x09000000    // x20 = UART base (preserved across function calls)
    
    // Stack setup - use a high address 
    mov x0, #0x40800000     // Set SP to 1GB + 8MB mark
    mov sp, x0              // Set stack pointer
    
    // Print stack pointer value for debugging
    mov w2, #'S'           // Stack pointer marker
    mov x1, x20            // UART base
    str w2, [x1]           // Print 'S'
    
    // CRITICAL FIX: Replace uart_puthex call with inline assembly
    // REASON: uart_puthex is a C function that depends on global variables
    // (specifically g_uart_base) which may not be properly initialized before
    // BSS section is zeroed. This violates embedded systems best practices
    // and can cause undefined behavior on different platforms/bootloaders.
    
    // OLD CODE (PROBLEMATIC - calls C before BSS init):
    // mov x0, sp             // Pass stack pointer as argument
    // bl uart_puthex         // Print stack pointer value
    
    // NEW CODE (SAFE - pure assembly, no global dependencies):
    mov x10, sp             // Save stack pointer value to print
    
    // Print "0x" prefix
    mov w2, #'0'
    str w2, [x1]
    mov w2, #'x'
    str w2, [x1]
    
    // Print 8 hex digits (32-bit address) - start from highest nibble
    mov x11, #28            // Start at bit 28 (highest nibble of 32-bit value)
hex_print_loop:
    mov x9, x10             // Copy value to work with
    lsr x9, x9, x11         // Shift right to get current nibble
    and x9, x9, #0xF        // Mask to 4 bits (one hex digit)
    
    // Convert nibble to ASCII character
    cmp x9, #10             // Compare with 10
    blt hex_digit_0_9       // Branch if 0-9
    add w9, w9, #('A' - 10) // Convert 10-15 to A-F
    b hex_print_digit
hex_digit_0_9:
    add w9, w9, #'0'        // Convert 0-9 to ASCII
hex_print_digit:
    str w9, [x1]            // Output the hex digit to UART
    
    // Move to next nibble
    sub x11, x11, #4        // Move to next 4-bit group
    cmp x11, #0             // Check if we've done all 8 digits
    bge hex_print_loop      // Continue if more digits to print
    
    // END OF SAFE HEX PRINTING CODE
    
    // UART debug output
    mov x1, x20            // UART base from saved register
    mov w2, #'X'           // Unique char to prove this runs
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // Initialize BSS section to zero
    mov w2, #'B'           // B for BSS initialization start
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    ldr x0, =_bss_start    // Load the start of the bss section
    ldr x1, =_bss_end      // Load the end of the bss section
    cmp x0, x1             // Check if bss section is empty
    beq skip_bss_init_new  // Skip if empty
    
bss_loop_new:
    stp xzr, xzr, [x0], #16 // Zero 16 bytes at a time
    cmp x0, x1              // Check if we've reached the end
    blo bss_loop_new        // Continue if not at end
    
skip_bss_init_new:
    mov w2, #'b'           // b for BSS initialization complete
    mov x1, x20            // UART base from saved register
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // MMU initialization marker
    mov w2, #'M'           // M for MMU initialization
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // Simple protection for link register
    str x30, [sp, #-16]!    // Push x30 onto the stack
    
    // Output debug character
    mov w2, #'s'           // Saved link register
    str w2, [x1]
    uart_delay
    
    // Debug before init_pmm call
    mov w2, #'1'           // About to call init_pmm
    str w2, [x1]
    uart_delay
    
    // Clear x30 just to be safe
    mov x30, #0
    
    // Manually set up return address
    adr x30, return_label  // Set link register to return label
    
    // Print right before test_return
    mov w2, #'x'           // About to call test function
    str w2, [x1]
    uart_delay
    
    // Call test_return function - using direct branch, not bl
    b test_return
    
return_label:
    // This is where we should return to
    mov w2, #'y'           // Returned from test function
    str w2, [x1]
    uart_delay
    
    // Call init_pmm
    bl init_pmm
    
    // Debug after init_pmm call
    mov w2, #'2'           // Back from init_pmm
    str w2, [x1]
    uart_delay
    
    // Restore link register
    ldr x30, [sp], #16     // Pop x30 back into place
    
    // Continue with debug output
    mov w2, #'P'           // PMM init complete
    str w2, [x1]
    uart_delay
    
    // CRITICAL SECTION - about to call init_vmm()
    // Verify current state
    mov w2, #'c'          // Character to prove we reached this point
    str w2, [x1]
    uart_delay

    // Print stack pointer value before alignment check
    mov w2, #'S'           // Stack pointer marker  
    str w2, [x1]
    uart_delay
    
    // NOTE: These uart_puthex calls are SAFE because they occur AFTER BSS initialization
    // (BSS was zeroed at lines 83-102). The problematic call was the earlier one before BSS init.
    // C functions can be safely called after BSS is properly initialized.
    mov x0, sp             // Pass stack pointer as argument
    bl uart_puthex         // Print stack pointer value
    mov x1, x20            // Restore UART base after function call

    // Fix stack alignment explicitly
    mov w2, #'F'           // Fix alignment marker
    str w2, [x1]
    uart_delay
    
    // Get misalignment value and print it
    mov x3, sp             // Copy SP to x3
    and x4, x3, #0xF       // Get misalignment (bottom 4 bits)
    add w2, w4, #'0'       // Convert to ASCII
    str w2, [x1]           // Print misalignment value
    uart_delay
    
    // Force 16-byte alignment
    bic x3, x3, #0xF       // Clear bottom 4 bits (BIC = bit clear)
    mov sp, x3             // Update SP with aligned value
    
    // Print new stack pointer after adjustment
    mov w2, #'N'           // New stack pointer marker
    str w2, [x1]
    uart_delay
    mov x0, sp             // Pass stack pointer as argument
    bl uart_puthex         // Print stack pointer value
    mov x1, x20            // Restore UART base after function call
    
    // Double-check alignment and print the result
    mov x3, sp             // Copy SP to x3
    and x4, x3, #0xF       // Get bottom 4 bits
    add w2, w4, #'0'       // Convert to ASCII (should print '0' if aligned)
    str w2, [x1]           // Show alignment result
    uart_delay
    
    // Continue execution
    
    // About to call init_vmm() - print 'v'
    mov w2, #'v'
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // CRITICAL FIX: Set VBAR_EL1 to vector_table directly before MMU initialization
    mov w2, #'V'
    str w2, [x1]
    mov w2, #'B'
    str w2, [x1] 
    mov w2, #':'
    str w2, [x1]
    
    // Load the vector_table address
    ldr x0, =vector_table
    // Set VBAR_EL1 to the physical address of vector_table
    msr vbar_el1, x0
    isb
    
    // Verify VBAR_EL1 was set
    mrs x0, vbar_el1
    mov x2, x0              // Save VBAR_EL1 value for printing
    
    // Print first hex digit of VBAR_EL1
    lsr x0, x0, #28
    and x0, x0, #0xF
    cmp x0, #10
    blt 1f
    add w0, w0, #('A' - 10)
    b 2f
1:  add w0, w0, #'0'
2:  str w0, [x1]           // Output the digit
    
    mov w2, #':'
    str w2, [x1]
    
    // Initialize virtual memory management (builds page tables)
    bl init_vmm
    
    // Right after init_vmm() call - print 'V'
    mov w2, #'V'
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // Debug output - VMM initialized
    mov x1, x20            // UART base from saved register
    mov w2, #'V'           // V for Virtual Memory Manager initialized
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    // USER REQUESTED DEBUG PRINTS
    mov w2, #'S'           // Indicate Start
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    mov w2, #'1'           // About to jump to kernel_main
    str w2, [x1]
    uart_delay             // Add delay after UART write
    
    bl kernel_main         // Call kernel_main
    
    mov w2, #'2'           // If we return, this confirms no crash
    str w2, [x1]
    uart_delay             // Add delay after UART write

halt:
    wfe                    // Wait for event
    b halt                 // Infinite loop
