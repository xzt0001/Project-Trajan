#include "../include/types.h"
#include "../include/uart.h"

// PL011 UART base address (QEMU's default)
#define UART0_BASE 0x09000000UL  // Add UL suffix for unsigned long

// Define register offsets
#define UART_DR_OFFSET     0x00    // Data Register
#define UART_FR_OFFSET     0x18    // Flag Register

// Flag bits
#define UART_FR_TXFF       (1 << 5)   // Transmit FIFO full

// Direct register access functions - more reliable than macros
static inline void uart_write_reg(uint32_t offset, uint32_t value) {
    *((volatile uint32_t*)(UART0_BASE + (uintptr_t)offset)) = value;
}

static inline uint32_t uart_read_reg(uint32_t offset) {
    return *((volatile uint32_t*)(UART0_BASE + (uintptr_t)offset));
}

void uart_init(void) {
    // QEMU's UART is already initialized
    // This function is now just a marker that we've reached this point
    // Don't print anything here
}

void uart_putc(char c) {
    // Wait until UART is ready to transmit
    while (uart_read_reg(UART_FR_OFFSET) & UART_FR_TXFF);
    // Write character to data register
    uart_write_reg(UART_DR_OFFSET, c);
}

void uart_puts(const char *str) {
    if (!str) return;  // Safety check
    
    while (*str) {
        if (*str == '\n') uart_putc('\r');  // Add carriage return
        uart_putc(*str++);
    }
}

void uart_puthex(uint64_t value) {
    // Print "0x" prefix using putc directly
    uart_putc('0');
    uart_putc('x');
    
    // Print exactly 8 hex digits with leading zeros for addresses
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        
        // Convert digit to character and print it
        if (digit < 10) {
            uart_putc('0' + digit);
        } else {
            uart_putc('a' + (digit - 10));
        }
    }
}
