// Basic early trap handlers for debugging purposes
.section .text
.global early_trap_init

// Simple exception handlers that just print a marker
.global trap_sync_handler
.global trap_irq_handler
.global trap_fiq_handler
.global trap_serror_handler

// Function to set up early trap handlers
early_trap_init:
    // Save link register
    str x30, [sp, #-16]!
    
    // Output marker to show we're setting up traps
    mov x0, #'E'
    bl uart_putc
    mov x0, #'T'
    bl uart_putc
    
    // Return
    ldr x30, [sp], #16
    ret

// Handlers that print a character and then halt
trap_sync_handler:
    // Print '!' to indicate synchronous exception
    mov x0, #'!'
    bl uart_putc
    
    // Print 'S' for Synchronous
    mov x0, #'S'
    bl uart_putc
    
    // Loop forever - system is in undefined state
    b .

trap_irq_handler:
    // Print '!' and 'I' for IRQ
    mov x0, #'!'
    bl uart_putc
    mov x0, #'I'
    bl uart_putc
    b .

trap_fiq_handler:
    // Print '!' and 'F' for FIQ
    mov x0, #'!'
    bl uart_putc
    mov x0, #'F'
    bl uart_putc
    b .

trap_serror_handler:
    // Print '!' and 'E' for SError
    mov x0, #'!'
    bl uart_putc
    mov x0, #'E'
    bl uart_putc
    b . 