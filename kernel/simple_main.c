#include <stdint.h>

// ABI-compatible function declaration
// Make it global, ensure it doesn't get optimized away or renamed
void kernel_main(void) __attribute__((used, externally_visible, noreturn));

// Print a single character to UART
static inline void print_char(char c) {
    volatile uint32_t* uart_dr = (volatile uint32_t*)(uintptr_t)0x09000000;
    *uart_dr = c;
}

// Minimal kernel_main function with extra diagnostics
void kernel_main(void) {
    // Print 'M' for Main
    print_char('M');
    
    // Print stack pointer first digit (hex)
    uint64_t sp_val;
    asm volatile ("mov %0, sp" : "=r" (sp_val));
    
    uint8_t digit1 = (sp_val >> 20) & 0xF;
    if (digit1 < 10) {
        print_char('0' + digit1);
    } else {
        print_char('A' + (digit1 - 10));
    }
    
    uint8_t digit2 = (sp_val >> 16) & 0xF;
    if (digit2 < 10) {
        print_char('0' + digit2);
    } else {
        print_char('A' + (digit2 - 10));
    }
    
    // Print 'E' for End
    print_char('E');
    
    // Infinite loop - this function never returns
    while (1) {
        print_char('.');  // Print dots to show we're still alive
        
        // Simple delay
        for (volatile int i = 0; i < 10000000; i++);
    }
    
    // This is actually unreachable, but keeps the compiler happy with noreturn
    __builtin_unreachable();
} 