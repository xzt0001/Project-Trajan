.global save_context
.global restore_context
.global full_restore_context
.global test_context_switch
.global dummy_asm
.global known_branch_test
.type save_context, %function
.type restore_context, %function
.type full_restore_context, %function
.type test_context_switch, %function
.type dummy_asm, %function
.type known_branch_test, %function

// save_context(task_t* task)
// x0 = pointer to task_t (passed from C)
save_context:
    // Save callee-saved registers (x19-x29)
    stp x19, x20, [x0, #8 * 19]  // Store pair of registers to memory
    stp x21, x22, [x0, #8 * 21]
    stp x23, x24, [x0, #8 * 23]
    stp x25, x26, [x0, #8 * 25]
    stp x27, x28, [x0, #8 * 27]
    str x29, [x0, #8 * 29]       // Frame pointer (FP)
    
    // Save link register x30 (LR) to PC field in task_t
    str x30, [x0, #8 * 31]   // PC is after 31 registers (x0-x30)
    
    // Save stack pointer (SP)
    mov x1, sp
    str x1, [x0]
    
    // Save current PSTATE to SPSR field
    mrs x1, DAIF
    str x1, [x0, #8 * 32]    // SPSR is after PC
    
    ret

// restore_context(task_t* task)
// x0 = pointer to task_t (passed from C)
// This only restores registers but doesn't switch execution
restore_context:
    // Debug output to UART
    ldr x1, =0x09000000
    mov w2, #'R'      // R for Restore context
    str w2, [x1]
    
    // Print task ID (assuming it's at offset +16 after regs)
    ldr x2, [x0, #8 * 31 + 16]
    and x2, x2, #0xF  // Get low 4 bits
    add w2, w2, #'0'
    str w2, [x1]
    
    // Debug: Print PC value (high 4 bits)
    ldr x2, [x0, #8 * 31]  // Load PC value
    lsr x2, x2, #28       // Shift to get high nibble
    and x2, x2, #0xF      // Mask to 4 bits
    cmp x2, #10
    bge 1f
    add w2, w2, #'0'      // Convert 0-9 to ASCII
    b 2f
1:
    sub w2, w2, #10
    add w2, w2, #'A'      // Convert 10-15 to A-F
2:
    str w2, [x1]
    
    // Debug: Print stack ptr initial value (high 4 bits)
    ldr x2, [x0]          // Load stack ptr
    lsr x2, x2, #28       // Shift to get high nibble
    and x2, x2, #0xF      // Mask to 4 bits
    cmp x2, #10
    bge 3f
    add w2, w2, #'0'      // Convert 0-9 to ASCII
    b 4f
3:
    sub w2, w2, #10
    add w2, w2, #'A'      // Convert 10-15 to A-F
4:
    str w2, [x1]
    
    // Check stack alignment before restore
    ldr x2, [x0]          // Load stack ptr
    and x3, x2, #0xF      // Get bottom 4 bits
    cmp x3, #0            // Should be 0 for 16-byte alignment
    beq 5f                // If aligned, branch forward
    mov w2, #'U'          // U for Unaligned
    str w2, [x1]
    b 6f
5:
    mov w2, #'A'          // A for Aligned
    str w2, [x1]
6:
    
    // Restore stack pointer first
    ldr x1, [x0]
    mov sp, x1
    
    // Restore callee-saved registers (x19-x29)
    ldp x19, x20, [x0, #8 * 19]  // Load pair of registers from memory
    ldp x21, x22, [x0, #8 * 21]
    ldp x23, x24, [x0, #8 * 23]
    ldp x25, x26, [x0, #8 * 25]
    ldp x27, x28, [x0, #8 * 27]
    ldr x29, [x0, #8 * 29]       // Frame pointer (FP)
    
    // Restore link register (LR) from PC field
    ldr x30, [x0, #8 * 31]
    
    // Regular return - this does NOT change execution to the task
    ret

// full_restore_context(task_t* task)
// x0 = pointer to task_t (passed from C)
// This function does a complete context restore and jumps to the task
full_restore_context:
    // --- CRITICAL CORE FUNCTIONALITY ---
    // 1. Print minimal debug info
    ldr x5, =0x09000000
    mov w6, #'F'
    str w6, [x5]
    
    // 2. Load stack pointer from task and set SP
    ldr x1, [x0]               // x1 = task->stack_ptr
    
    // PRINT STACK POINTER IN HEX (BEFORE NULL CHECK)
    mov x4, x1                // Copy stack pointer to x4
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'P'
    str w6, [x5]
    mov w6, #'='
    str w6, [x5]
    
    // Print full SP value in hex (8 bytes)
    mov x7, #60           // Start with highest nibble (64-4 = 60)
sp_print_loop:
    mov x8, x4            // Get SP value
    lsr x8, x8, x7        // Shift right to get current nibble
    and x8, x8, #0xF      // Mask to 4 bits
    
    // Convert to ASCII
    cmp x8, #10
    blt 100f
    add w8, w8, #('A'-10) // A-F
    b 101f
100:
    add w8, w8, #'0'      // 0-9
101:
    str w8, [x5]          // Print the nibble
    
    sub x7, x7, #4        // Move to next nibble
    cmp x7, #0
    bge sp_print_loop     // Continue until all 16 nibbles are printed
    
    // Print newline
    mov w6, #'\r'
    str w6, [x5]
    mov w6, #'\n'
    str w6, [x5]
    
    // Now continue with PC printing
    
    // PRINT TASK->PC BEFORE ANYTHING ELSE (HIGH NIBBLE)
    ldr x2, [x0, #8 * 31]      // Load task->pc (assumed at offset 8*31)
    
    // Print "PC=" prefix
    mov w6, #'P'
    str w6, [x5]
    mov w6, #'C'
    str w6, [x5]
    mov w6, #'='
    str w6, [x5]
    
    // Print full PC value in hex (8 bytes)
    mov x7, #60           // Start with highest nibble (64-4 = 60)
pc_print_loop:
    mov x8, x2            // Get PC value
    lsr x8, x8, x7        // Shift right to get current nibble
    and x8, x8, #0xF      // Mask to 4 bits
    
    // Convert to ASCII
    cmp x8, #10
    blt 1f
    add w8, w8, #('A'-10) // A-F
    b 2f
1:
    add w8, w8, #'0'      // 0-9
2:
    str w8, [x5]          // Print the nibble
    
    sub x7, x7, #4        // Move to next nibble
    cmp x7, #0
    bge pc_print_loop     // Continue until all 16 nibbles are printed
    
    // Print newline
    mov w6, #'\r'
    str w6, [x5]
    mov w6, #'\n'
    str w6, [x5]
    
    // Load SPSR now
    ldr x3, [x0, #8 * 32]      // Load task->spsr (offset 32*8)
    
    // Print SPSR value
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'P'
    str w6, [x5]
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'R'
    str w6, [x5]
    mov w6, #'='
    str w6, [x5]
    
    // Print SPSR value in hex (high nibble)
    mov x7, #28           // High 32 bits
    mov x8, x3            // Get SPSR value
    lsr x8, x8, x7        // Shift right to get high nibble
    and x8, x8, #0xF      // Mask to 4 bits
    
    // Convert to ASCII
    cmp x8, #10
    blt 3f
    add w8, w8, #('A'-10) // A-F
    b 4f
3:
    add w8, w8, #'0'      // 0-9
4:
    str w8, [x5]          // Print the nibble
    
    // Print newline
    mov w6, #'\r'
    str w6, [x5]
    mov w6, #'\n'
    str w6, [x5]
    
    // Validate stack pointer before using it
    cmp x1, #0
    bne 5f                     // If not null, continue
    
    // Stack pointer is NULL - critical error
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'='
    str w6, [x5] 
    mov w6, #'0'               // Indicate NULL stack pointer
    str w6, [x5]
    b fatal_error              // Jump to error handler
    
5:  // Stack pointer is valid
    // Check stack alignment
    mov x4, x1
    and x4, x4, #0xF           // Get bottom 4 bits
    cmp x4, #0                 // Should be 0 for 16-byte alignment
    beq 6f                     // If aligned, continue
    
    // Alignment error
    mov w6, #'A'
    str w6, [x5]
    mov w6, #'L'
    str w6, [x5]
    mov w6, #'I'
    str w6, [x5]
    mov w6, #'G'
    str w6, [x5]
    mov w6, #'N'
    str w6, [x5]
    mov w6, #'!'
    str w6, [x5]
    b fatal_error              // Misaligned stack is fatal
    
6:  // Print before SP_EL1 update
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'P'
    str w6, [x5]
    mov w6, #'_'
    str w6, [x5]
    mov w6, #'S'
    str w6, [x5]
    mov w6, #'E'
    str w6, [x5]
    mov w6, #'T'
    str w6, [x5]
    
    // CRITICAL CHANGE: Set registers in the correct order
    // Important: First set SPSR_EL1, then ELR_EL1, then SP
    
    // Verify SPSR value before loading it
    mov w6, #'V'
    str w6, [x5]
    mov w6, #'_'
    str w6, [x5]
    mov w6, #'S'
    str w6, [x5]
    
    // Print SPSR in hex (one character)
    mov x8, x3            // x3 holds SPSR
    lsr x8, x8, #8        // Get high byte
    and x8, x8, #0xF
    cmp x8, #10
    blt 10f
    add w8, w8, #('A'-10)
    b 11f
10:
    add w8, w8, #'0'
11:
    str w8, [x5]
    
    // Add markers before each system register setting
    mov x0, #'1'
    bl uart_putc
    msr spsr_el1, x3          // Set SPSR_EL1 from task->spsr
    
    mov x0, #'2'
    bl uart_putc
    msr elr_el1, x2           // Set ELR_EL1 from task->pc
    
    mov x0, #'3'
    bl uart_putc
    mov sp, x1                // Set stack pointer from task->stack_ptr
        
    // Print after register updates confirmation
    mov w6, #'\r'
    str w6, [x5]
    mov w6, #'\n'
    str w6, [x5]
    
    // Check stack memory for the test pattern (0xAA)
    mov w6, #'M'  // M for Memory check
    str w6, [x5]
    mov w6, #':'
    str w6, [x5]
    
    // Check the first few bytes of the stack page for the pattern
    ldr x9, [sp]   // Load the first 8 bytes from stack
    mov x0, x9
    bl uart_hex64  // Print the value
    
    // Print final markers right before ERET
    mov w6, #'E'
    str w6, [x5]
    mov w6, #'R'
    str w6, [x5]
    mov w6, #'E'
    str w6, [x5]
    mov w6, #'T'
    str w6, [x5]
    mov w6, #':'
    str w6, [x5]
    
    // Dump all key registers before eret
    mov x0, x19          // task->pc
    bl uart_hex64
    
    mov x0, sp
    bl uart_hex64
    
    mrs x0, spsr_el1
    bl uart_hex64
    
    // Final marker using uart_putc right before eret
    mov x0, #'Z'
    bl uart_putc
    
    // Now perform the actual exception return
    eret
    
    // Should never reach here - ERET doesn't return
fatal_error:
    mov w6, #'!'
    str w6, [x5]
    mov w6, #'!'
    str w6, [x5]
    mov w6, #'!'
    str w6, [x5]
    b .

// Add dummy function for testing
.align 4
.global dummy_asm
dummy_asm:
    // Simple function that just outputs 'A' to UART and returns
    mov x0, #'A'
    bl uart_putc
    ret

// Add minimal test context switch with hardcoded values
.global test_context_switch
.align 4
test_context_switch:
    // Very first instruction - direct character output
    mov x0, #'T'
    bl uart_putc
    
    // Get address of dummy_asm function for target PC
    adr x0, dummy_asm   // Use direct addressing to get function address
    mov x9, x0          // Save for debug
    
    // Output PC address we're going to use
    mov x0, #'P'
    bl uart_putc
    mov x0, x9          // Print the PC value we're about to use
    bl uart_putx
    
    mov x0, x9          // Restore PC value
    ldr x1, =0x40800000 // known good stack (top of valid page)
    
    // Try different SPSR values as suggested
    // First try 0x3C0 - EL1h, all interrupts masked
    mov x2, #0x3C0      
    
    // Debug before setting registers
    mov x0, #'Z'
    bl uart_putc
    
    // Set ELR_EL1 (PC to return to)
    msr elr_el1, x9     // Use address of dummy_asm
    
    // Set SP_EL1 (stack pointer for EL1) - explicitly as suggested
    msr sp_el1, x1
    
    // Set SPSR_EL1 (processor state to restore)
    msr spsr_el1, x2
    
    mov x0, #'E'        // About to ERET
    bl uart_putc
    
    // Return from exception
    eret
    
    // If the above ERET fails, we'll continue here and try with a different SPSR
    mov x0, #'R'        // Retry marker
    bl uart_putc
    
    // Try with 0x340 - EL1h, only IRQ masked
    mov x2, #0x340
    
    // Set SPSR_EL1 with new value
    msr spsr_el1, x2
    
    mov x0, #'2'        // Second attempt marker
    bl uart_putc
    
    // Return from exception with second SPSR value
    eret

// Add a simple known branch test function
.align 3
known_branch_test:
    mov x0, #'X'
    bl uart_putc
    ret
