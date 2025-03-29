.section .text.boot
.global _start
.extern kernel_main
.extern _bss_start
.extern _bss_end

_start:
    // Setup the stack with proper alignment - IMPORTANT for AArch64
    mov x0, #0x800000      // 8MB
    and x0, x0, #~0xF      // 16-byte alignment (important for AArch64)
    mov sp, x0 // set the stack pointer to the top of the 8MB heap
    
    // Mark the frame pointer as zero to terminate backtraces
    mov x29, #0 // set the frame pointer to zero
    mov x30, #0 // set the link register to zero

    // Simple UART print ('S' for Start)
    ldr x1, =0x09000000
    mov w2, #'S'
    str w2, [x1]
    
    // Initialize BSS section to zero
    ldr x0, =_bss_start // load the start of the bss section into x0
    ldr x1, =_bss_end // load the end of the bss section into x1
    cmp x0, x1 // if x0 == x1, skip bss_loop
    beq skip_bss_init // if x0 != x1, skip bss_loop
bss_loop: //
    stp xzr, xzr, [x0], #16    // Zero 16 bytes at a time 
    cmp x0, x1 // if x0 == x1, skip bss_loop
    blo bss_loop // if x0 < x1, skip bss_loop
skip_bss_init:
    
    // Stack check marker
    mov w2, #'8'                // Indicates 0x800000
    str w2, [x1]

    // Double check proper stack setup
    mov x0, sp
    str x0, [sp, #-16]!         // Test stack by storing value
    ldr x0, [sp], #16           // And reading it back
    
    // Stack marker
    mov w2, #'>'
    str w2, [x1]

    // Ensure stack is 16-byte aligned before call
    mov x0, sp
    and x0, x0, #0xF            
    cbz x0, stack_ok            // If x0 is zero, stack is aligned
    
    // Emit error if stack misaligned
    mov w2, #'A'                // Alignment error
    str w2, [x1]
    b hang
    
stack_ok:
    // Stack is good, proceed with kernel_main
    bl kernel_main

    // This should never execute (infinite loop in kernel_main)
    ldr x1, =0x09000000
    mov w2, #'!'
    str w2, [x1]

hang:
    wfe
    b hang
