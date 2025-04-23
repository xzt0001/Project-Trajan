.section .text.boot
.global _start
.extern kernel_main

_start:
    // Set up a simple stack
    mov x0, #0x800000      // 8MB
    mov sp, x0

    // Print 'S' to UART for Start
    ldr x1, =0x09000000
    mov w2, #'S'
    str w2, [x1]

    // Print 'T' to UART for Start
    mov w2, #'T'
    str w2, [x1]
    
    // Pass UART address as parameter
    ldr x0, =0x09000000
    
    // Simply call kernel_main directly
    bl kernel_main
    
    // Should never return, but if it does, print 'R' and hang
    mov w2, #'R'
    str w2, [x1]
    
hang:
    wfe
    b hang 