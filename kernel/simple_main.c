#include <stdint.h>

// Add required function prototypes
void uart_init(void);
void uart_puts(const char* str);
void uart_putc(char c);
void init_pmm(void);
void init_vmm(void);
void init_exception(void);
void init_uart_driver(void);
void test_scheduler(void);

// ABI-compatible function declaration
// Make it global, ensure it doesn't get optimized away or renamed
void kernel_main(void) __attribute__((used, externally_visible, noreturn));

// Vector table defined in vector.S
extern void* vector_table;

// Print a single character to UART
static inline void print_char(char c) {
    volatile uint32_t* uart_dr = (volatile uint32_t*)(uintptr_t)0x09000000;
    *uart_dr = c;
}

// Minimal kernel_main function matching the structure in screenshots
void kernel_main(void) {
    uart_init();
    uart_puts("CustomOS Kernel Booting...\n");
    
    init_pmm();
    init_vmm();
    init_exception();
    init_uart_driver();
    
    // Add the scheduler test call here
    test_scheduler();  // Start the round-robin scheduler test
    
    while (1) {
        uart_putc('.');
    }
    
    // This is actually unreachable, but keeps the compiler happy with noreturn
    __builtin_unreachable();
} 