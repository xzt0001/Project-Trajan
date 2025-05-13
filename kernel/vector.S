// Place the vector table in a dedicated section that can be aligned
.section .text.vectors, "ax"
// Vector table symbol is now defined in the linker script
// Still use the global to maintain ABI compatibility
.global vector_table
.align 11 // Ensure 2KB alignment (2^11 = 2048)

// Entry point for the vector table handlers
// This content must be at the vector_table symbol location
vector_table:
    // IMPORTANT CHANGE: We're redefining the table layout to place EL1h entries at the beginning
    // While this is not architecturally standard, it ensures the CPU finds our handlers
    
    // THESE ENTRIES SHOULD MATCH EL1h HANDLERS (which would normally be at 0x200-0x3FF)
    b sync_handler          // 0x000 - Using EL1h synchronous handler here
    .balign 0x80
    b irq_handler           // 0x080 - Using EL1h IRQ handler here (critical for IRQs)
    .balign 0x80
    b fiq_debug_handler     // 0x100 - Using EL1h FIQ handler here - added debug
    .balign 0x80
    b serror_debug_handler  // 0x180 - Using EL1h SError handler here - added debug
    .balign 0x80
    
    // THESE ENTRIES SHOULD BE THE ORIGINAL EL1t HANDLERS (which would normally be at 0x000-0x1FF)
    // But now they're at 0x200-0x3FF - won't be used if system runs in EL1h
    b sync_debug_handler    // 0x200 - Original EL1t sync spot - added debug handler
    .balign 0x80
    b irq_debug_handler     // 0x280 - Original EL1t IRQ spot - added debug handler
    .balign 0x80
    b fiq_debug_handler     // 0x300 - Original EL1t FIQ spot - added debug handler
    .balign 0x80
    b serror_debug_handler  // 0x380 - Original EL1t SError spot - added debug handler
    .balign 0x80
    
    // The rest of the table remains architecturally correct
    // SECTION 3: LOWER EL USING AARCH64 - 0x400-0x5FF
    b sync_el0_handler      // 0x400 - Synchronous EL0/A64
    .balign 0x80
    b irq_el0_handler       // 0x480 - IRQ EL0/A64
    .balign 0x80
    b fiq_el0_handler       // 0x500 - FIQ EL0/A64
    .balign 0x80
    b serror_el0_handler    // 0x580 - SError EL0/A64
    .balign 0x80
    
    // SECTION 4: LOWER EL USING AARCH32 - 0x600-0x7FF
    b sync_el0_handler      // 0x600 - Synchronous EL0/A32
    .balign 0x80
    b irq_el0_handler       // 0x680 - IRQ EL0/A32
    .balign 0x80
    b fiq_el0_handler       // 0x700 - FIQ EL0/A32
    .balign 0x80
    b serror_el0_handler    // 0x780 - SError EL0/A32

// Add new debug handler for synchronous exceptions
sync_debug_handler:
    // Direct debug output - no register saving
    mov x0, #'!'
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'S'
    str w0, [x1]
    mov x0, #'Y'
    str w0, [x1]
    mov x0, #'N'
    str w0, [x1]
    mov x0, #'C'
    str w0, [x1]
    mov x0, #'!'
    str w0, [x1]
    
    // Print extended diagnostic info
    ldr x1, =0x09000000
    
    // Print upper nibble of PC from ELR_EL1
    mrs x2, elr_el1
    lsr x2, x2, #28
    and x2, x2, #0xF
    
    // Convert to ASCII hex
    cmp x2, #10
    blt 1f
    add x2, x2, #('A' - 10)
    b 2f
1:
    add x2, x2, #'0'
2:
    // Output the hex digit
    str w2, [x1]
    
    // Print ESR_EL1 fault status code
    mov x0, #':'
    str w0, [x1]
    mov x0, #'E'
    str w0, [x1]
    mov x0, #'C'
    str w0, [x1]
    mov x0, #'='
    str w0, [x1]
    
    // Read and print ESR_EL1 exception class
    mrs x2, esr_el1
    lsr x2, x2, #26     // Extract EC field (bits [31:26])
    and x2, x2, #0x3F
    
    // Convert to ASCII hex
    cmp x2, #10
    blt 3f
    add x2, x2, #('A' - 10)
    b 4f
3:
    add x2, x2, #'0'
4:
    str w2, [x1]
    
    // Print FAR_EL1 (Fault Address Register)
    mov x0, #':'
    str w0, [x1]
    mov x0, #'@'
    str w0, [x1]
    
    // Read and print top byte of FAR_EL1
    mrs x2, far_el1
    lsr x2, x2, #28
    and x2, x2, #0xF
    cmp x2, #10
    blt 5f
    add x2, x2, #('A' - 10)
    b 6f
5:
    add x2, x2, #'0'
6:
    str w2, [x1]
    
    // Print the full error code from ESR_EL1
    mov x0, #':'
    str w0, [x1]
    mov x0, #'E'
    str w0, [x1]
    mov x0, #'='
    str w0, [x1]
    
    // Read and print full ESR_EL1 for debugging
    mrs x2, esr_el1
    mov x3, x2
    
    // Output first hex digit (bits 16-19)
    lsr x3, x3, #16
    and x3, x3, #0xF
    cmp x3, #10
    blt 7f
    add x3, x3, #('A' - 10)
    b 8f
7:
    add x3, x3, #'0'
8:
    str w3, [x1]
    
    // Output second hex digit (bits 12-15)
    mov x3, x2
    lsr x3, x3, #12
    and x3, x3, #0xF
    cmp x3, #10
    blt 9f
    add x3, x3, #('A' - 10)
    b 10f
9:
    add x3, x3, #'0'
10:
    str w3, [x1]
    
    // Output third hex digit (bits 8-11)
    mov x3, x2
    lsr x3, x3, #8
    and x3, x3, #0xF
    cmp x3, #10
    blt 11f
    add x3, x3, #('A' - 10)
    b 12f
11:
    add x3, x3, #'0'
12:
    str w3, [x1]
    
    // Output fourth hex digit (bits 4-7)
    mov x3, x2
    lsr x3, x3, #4
    and x3, x3, #0xF
    cmp x3, #10
    blt 13f
    add x3, x3, #('A' - 10)
    b 14f
13:
    add x3, x3, #'0'
14:
    str w3, [x1]
    
    // Output fifth hex digit (bits 0-3)
    mov x3, x2
    and x3, x3, #0xF
    cmp x3, #10
    blt 15f
    add x3, x3, #('A' - 10)
    b 16f
15:
    add x3, x3, #'0'
16:
    str w3, [x1]
    
    // Loop forever
    b .

// Add new debug handler for IRQ exceptions
irq_debug_handler:
    // Direct debug output - no register saving
    mov x0, #'!'
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'I'
    str w0, [x1]
    mov x0, #'R'
    str w0, [x1]
    mov x0, #'Q'
    str w0, [x1]
    mov x0, #'!'
    str w0, [x1]
    
    // Loop forever
    b .

// Add new debug handler for FIQ exceptions
fiq_debug_handler:
    // Direct debug output - no register saving
    mov x0, #'!'
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'F'
    str w0, [x1]
    mov x0, #'I'
    str w0, [x1]
    mov x0, #'Q'
    str w0, [x1]
    mov x0, #'!'
    str w0, [x1]
    
    // Loop forever
    b .

// The SError debug handler is now defined in serror_debug_handler.S
// with more comprehensive debugging information
.global serror_debug_handler

// Add debugging output to IRQ handler entry point
.global irq_entry_el1h
irq_entry_el1h:
    // Debug code to confirm entry point
    mov x0, #'E'            // 'E' for EL1h
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'L'            // 'L' for EL1h
    str w0, [x1]
    mov x0, #'1'            // '1' for EL1h
    str w0, [x1]
    mov x0, #'h'            // 'h' for EL1h
    str w0, [x1]
    mov x0, #'-'
    str w0, [x1]
    mov x0, #'I'            // 'I' for IRQ
    str w0, [x1]
    mov x0, #'R'            // 'R' for IRQ
    str w0, [x1]
    mov x0, #'Q'            // 'Q' for IRQ
    str w0, [x1]
    mov x0, #'\r'
    str w0, [x1]
    mov x0, #'\n'
    str w0, [x1]
    b irq_handler           // Branch to the actual handler

// Added SVC handler to process supervisor calls (SVC instruction)
.global svc_handler
svc_handler:
    // Save registers that might be clobbered
    stp x29, x30, [sp, #-16]!
    stp x0, x1, [sp, #-16]!
    
    // Print SVC marker to UART
    mov x0, #'S'
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'V'
    str w0, [x1]
    mov x0, #'C'
    str w0, [x1]
    mov x0, #':'
    str w0, [x1]
    
    // Extract SVC number from instruction
    mrs x0, esr_el1         // Get exception syndrome register
    lsr x0, x0, #26         // Shift right to get EC field
    and x0, x0, #0x3F       // Mask to get only EC
    
    // Check if it's really an SVC (EC=0x15 for SVC from AArch64)
    cmp x0, #0x15
    bne not_svc
    
    // It's an SVC, get the immediate value
    mrs x0, esr_el1
    and x0, x0, #0xFFFF     // Extract immediate from instruction
    
    // Convert to ASCII and print
    add x0, x0, #'0'        // Assume small number for demo
    str w0, [x1]            // Print SVC number
    
    // Print newline
    mov x0, #'\r'
    str w0, [x1]
    mov x0, #'\n'
    str w0, [x1]
    
    // Restore registers and return
    ldp x0, x1, [sp], #16
    ldp x29, x30, [sp], #16
    ret
    
not_svc:
    // Not an SVC, print error
    mov x0, #'!'
    str w0, [x1]
    mov x0, #'E'
    str w0, [x1]
    mov x0, #'R'
    str w0, [x1]
    mov x0, #'R'
    str w0, [x1]
    mov x0, #'!'
    str w0, [x1]
    
    // Restore and chain to sync handler
    ldp x0, x1, [sp], #16
    ldp x29, x30, [sp], #16
    b sync_handler

// Synchronous exception handler that handles data aborts
.global sync_handler
sync_handler:
    // First check if this is an SVC call
    mrs x2, esr_el1         // Get exception syndrome register
    lsr x4, x2, #26         // Shift right to get EC field
    and x4, x4, #0x3F       // Mask to get only EC
    
    // If it's an SVC (EC=0x15 for SVC from AArch64), handle it specially
    cmp x4, #0x15
    beq handle_svc
    
    // Otherwise, continue with normal sync exception handling
    
    // Print 'X' to UART to indicate an exception occurred
    mov x0, #'X'
    ldr x1, =0x09000000
    str w0, [x1]

    // Load ESR_EL1 and ELR_EL1
    mrs x2, esr_el1
    mrs x3, elr_el1

    // Print upper nibble of ESR and ELR for quick fault code
    lsr x2, x2, #28
    lsr x3, x3, #28
    add x2, x2, #'0'
    add x3, x3, #'0'
    str w2, [x1]
    str w3, [x1]

    // Loop forever
    b .

// Handle SVC calls
handle_svc:
    // Save link register and frame pointer
    stp x29, x30, [sp, #-16]!
    
    // Call SVC handler
    bl svc_handler
    
    // Restore registers
    ldp x29, x30, [sp], #16
    
    // Return from exception
    eret

// Severe synchronous abort handler with maximum diagnostic info
.global sync_abort_handler
sync_abort_handler:
    // Print an extremely distinctive sequence to UART to indicate exception occurred
    mov x0, #'!'
    ldr x1, =0x09000000
    str w0, [x1]
    mov x0, #'E'
    str w0, [x1]
    mov x0, #'X'
    str w0, [x1]
    mov x0, #'C'
    str w0, [x1]
    mov x0, #'P'
    str w0, [x1]
    mov x0, #'T'
    str w0, [x1]
    mov x0, #'!'
    str w0, [x1]
    
    // Load and print upper nibble of ESR_EL1 and ELR_EL1
    mrs x2, esr_el1
    mrs x3, elr_el1
    lsr x2, x2, #28
    lsr x3, x3, #28
    add x2, x2, #'0'
    add x3, x3, #'0'
    str w2, [x1]
    str w3, [x1]

    // Print full ESR_EL1 register in hex (top byte only)
    mrs x4, esr_el1
    lsr x4, x4, #24
    and x4, x4, #0xFF
    
    // Print first hex digit
    mov x5, x4
    lsr x5, x5, #4
    and x5, x5, #0xF
    cmp x5, #10
    blt 1f
    add x5, x5, #('A'-10)
    b 2f
1:
    add x5, x5, #'0'
2:
    str w5, [x1]
    
    // Print second hex digit
    and x5, x4, #0xF
    cmp x5, #10
    blt 3f
    add x5, x5, #('A'-10)
    b 4f
3:
    add x5, x5, #'0'
4:
    str w5, [x1]
    
    // Halt
    b .

// Helper function to set the vector table base register
.global set_vbar_el1
set_vbar_el1:
    // x0 contains the address to set as the vector table base
    msr vbar_el1, x0
    isb
    ret

.align 3
.global set_safe_spsr
.type set_safe_spsr, %function

// ==========================================
// APPROACH 3: SET A KNOWN-SAFE SPSR_EL1 VALUE
// ==========================================
set_safe_spsr:
    // Direct UART diagnostic
    ldr x9, =0x09000000
    mov w10, #'S'
    str w10, [x9]
    mov w10, #'P'
    str w10, [x9]
    mov w10, #'S'
    str w10, [x9]
    mov w10, #'R'
    str w10, [x9]
    mov w10, #':'
    str w10, [x9]
    
    // Use the safer 0x340 value for SPSR_EL1
    // M[3:0] = 0100 (EL1h), FIQ/IRQ masked, SError masked
    mov x0, #0x340
    msr spsr_el1, x0
    
    // Confirm the value was set
    mrs x1, spsr_el1
    
    // Check if it matches
    cmp x1, x0
    beq .Lspsr_ok
    
    // Error - SPSR not set
    mov w10, #'!'
    str w10, [x9]
    ret
    
.Lspsr_ok:
    // Success - SPSR set correctly
    mov w10, #'O'
    str w10, [x9]
    mov w10, #'K'
    str w10, [x9]
    mov w10, #'\r'
    str w10, [x9]
    mov w10, #'\n'
    str w10, [x9]
    ret

.align 3
.global check_fix_stack_alignment
.type check_fix_stack_alignment, %function

// ==========================================
// APPROACH 4: CHECK AND FIX STACK ALIGNMENT
// ==========================================
check_fix_stack_alignment:
    // Direct UART diagnostic
    ldr x9, =0x09000000
    mov w10, #'S'
    str w10, [x9]
    mov w10, #'T'
    str w10, [x9]
    mov w10, #'K'
    str w10, [x9]
    mov w10, #':'
    str w10, [x9]
    
    // Fix alignment using round-down algorithm
    // Add 15 then clear bottom 4 bits (equivalent to rounding down to multiple of 16)
    mov x0, sp                // Copy current SP
    add x0, x0, #15           // Add 15
    lsr x0, x0, #4            // Divide by 16
    lsl x0, x0, #4            // Multiply by 16 (now aligned to 16 bytes)
    mov sp, x0                // Update SP with aligned value
    
    // Confirm alignment by checking if bottom 4 bits are zero using shift operations
    mov x1, sp
    lsl x1, x1, #60   // Shift left to keep only the bottom 4 bits
    lsr x1, x1, #60   // Shift right to move them back to position
    cbnz x1, .Lalign_failed
    
    // Success - print 'A' for aligned
    mov w10, #'A'
    str w10, [x9]
    mov w10, #'\r'
    str w10, [x9]
    mov w10, #'\n'
    str w10, [x9]
    ret
    
.Lalign_failed:
    // This should never happen, but just in case
    mov w10, #'!'
    str w10, [x9]
    ret

