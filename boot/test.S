.section .text.boot.main
.global test_uart_directly

// This is a pure assembly function to test UART output
test_uart_directly:
    // Save link register
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Load UART address
    ldr x1, =0x09000000

    // Write test pattern
    mov w2, #'T'
    str w2, [x1]
    mov w2, #'E'
    str w2, [x1]
    mov w2, #'S'
    str w2, [x1]
    mov w2, #'T'
    str w2, [x1]
    mov w2, #'\r'
    str w2, [x1]
    mov w2, #'\n'
    str w2, [x1]

    // Wait a bit - using smaller value that fits in immediate
    mov x3, #0xFFFF  // Smaller delay that fits in immediate

1:  subs x3, x3, #1
    bne 1b

    // Restore link register and return
    ldp x29, x30, [sp], #16
    ret 