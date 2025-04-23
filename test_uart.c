// Ultra-minimal UART test
__attribute__((used, externally_visible, noinline, section(".text.boot.main")))
void kernel_main(void) {
    // Direct UART write at known PL011 UART address for QEMU virt machine
    volatile unsigned int* uart = (volatile unsigned int*)0x09000000;
    
    // Print 'A' to confirm basic MMIO UART access works
    *uart = 'A';
    
    // Print CurrentEL value to see what exception level we're in
    unsigned long el;
    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    *uart = '0' + ((el >> 2) & 3);  // Should print '1' if in EL1
    
    // Loop forever
    while (1) {}
} 