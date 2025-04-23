#ifndef UART_H
#define UART_H

#include "types.h"

// Initialize UART
void uart_init(void);

// Output functions
void uart_putc(char c);
void uart_puts(const char* str);
void uart_puthex(uint64_t value);
void uart_print_hex(uint64_t value);
void uart_hex64(uint64_t value);
void uart_putx(uint64_t value);  // Print 8 hex digits with no prefix

#endif /* UART_H */
