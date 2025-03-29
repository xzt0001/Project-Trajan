.section .text
.global kernel_main

kernel_main:
    // Properly save the link register
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    
    // Print a 'K' character to UART
    ldr x1, =0x09000000  // UART0_DR
    mov w2, #'K'
    str w2, [x1]
    
    // Print a '+' character to UART
    mov w2, #'+'
    str w2, [x1]
    
    // Restore link register and return
    ldp x29, x30, [sp], #16
    ret 