.section .text
.global _start

_start:
    // Set up the stack pointer properly
    ldr x0, =_stack_top
    mov sp, x0
    
    // Print 'A' to show assembly is working
    ldr x1, =0x09000000  // UART0_DR
    mov x2, #'A'
    str w2, [x1]
    
    // Call our minimal test function
    bl minimal_test
    
    // Print 'Z' to show we returned from C
    ldr x1, =0x09000000  // UART0_DR
    mov x2, #'Z'
    str w2, [x1]
    
    // Hang
1:  wfe
    b 1b

.section .bss
.align 12
_stack:
    .skip 16384     // 16K stack
_stack_top:

// Minimal C test function
.section .text
.global minimal_test
minimal_test:
    // Save link register
    stp x30, xzr, [sp, #-16]!
    
    // Print 'B' to show we're in C
    ldr x1, =0x09000000  // UART0_DR
    mov x2, #'B'
    str w2, [x1]
    
    // Restore link register and return
    ldp x30, xzr, [sp], #16
    ret 