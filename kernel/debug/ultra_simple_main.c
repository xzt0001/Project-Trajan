//
// ultra_simple_main.c - Absolute minimal kernel_main with raw assembly
//

// Explicitly export the symbol for the linker
void kernel_main(void) __attribute__((naked, used, externally_visible, section(".text")));

// Avoid any C runtime dependencies
void kernel_main(void) {
    // Use raw assembly that doesn't rely on C compiler
    __asm__ __volatile__ (
        "ldr x1, =0x09000000\n"  // Load UART data register address
        "mov w2, #'M'\n"         // Character to print
        "str w2, [x1]\n"         // Write to UART
        "mov w2, #'D'\n"         // Another character
        "str w2, [x1]\n"         // Write to UART again
        "ret\n"                  // Return to caller
    );
} 