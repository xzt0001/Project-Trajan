.section .text.boot
.global _start
.extern kernel_main

_start:
    // Set up stack
    mov sp, #0x80000
    
    // Print 'S' for startup
    ldr x1, =0x09000000
    mov w0, #'S'
    str w0, [x1]
    
    // Call kernel_main
    bl kernel_main
    
    // Should never get here
    b . 