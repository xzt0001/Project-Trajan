#ifndef UART_H
#define UART_H

#include "types.h"

// Initialize UART
void uart_init(void);

// Output functions
void uart_putc(char c);
void uart_puts(const char* str);
void uart_puthex(uint64_t value);

#endif /* UART_H */
