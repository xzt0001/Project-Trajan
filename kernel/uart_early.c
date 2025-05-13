#include "../include/types.h"
#include "../include/uart.h"

// Physical UART base address - hardcoded for early boot
#define UART_PHYS 0x09000000

// Direct UART access functions for pre-MMU stage
void uart_putc_early(char c) {
    // Wait until UART transmit FIFO has space
    while (*((volatile uint32_t*)(UART_PHYS + 0x18)) & (1 << 5));
    
    // Write the character directly to the UART data register
    *((volatile uint32_t*)(UART_PHYS + 0x00)) = c;
}

void uart_puts_early(const char *s) {
    if (!s) return;
    
    while (*s) {
        if (*s == '\n') {
            uart_putc_early('\r');
        }
        uart_putc_early(*s++);
    }
}

void uart_hex64_early(uint64_t value) {
    // Output "0x" prefix
    uart_putc_early('0');
    uart_putc_early('x');
    
    // Output all 16 hex digits
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        uart_putc_early(c);
    }
}

// Debug marker function - uses direct access to stand out in logs
void uart_debug_marker(char marker) {
    volatile uint32_t *UART0_DR = (volatile uint32_t *)UART_PHYS;
    
    // Output a distinctive pattern
    for (int i = 0; i < 3; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = ' ';
    *UART0_DR = 'D';
    *UART0_DR = 'E';
    *UART0_DR = 'B';
    *UART0_DR = 'U';
    *UART0_DR = 'G';
    *UART0_DR = ' ';
    *UART0_DR = 'M';
    *UART0_DR = 'A';
    *UART0_DR = 'R';
    *UART0_DR = 'K';
    *UART0_DR = 'E';
    *UART0_DR = 'R';
    *UART0_DR = ':';
    *UART0_DR = ' ';
    *UART0_DR = marker;
    *UART0_DR = ' ';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '*';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    
    // More line breaks for separation
    for (int i = 0; i < 3; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    // Delay to ensure character is transmitted
    for (volatile int i = 0; i < 50000; i++) {
        // Extended delay loop
    }
}

// Initial UART setup - only used early in boot
void uart_init_early(unsigned long uart_addr) {
    // Simple test to confirm UART is working
    uart_putc_early('E'); // E for Early
    uart_putc_early('A'); // A for Access
    uart_putc_early('R'); // R for Ready
    uart_putc_early('L'); // L for Load
    uart_putc_early('Y'); // Y for Yes
    uart_putc_early(':'); // : separator
    uart_putc_early('O'); // O for OK
    uart_putc_early('K'); // K for K
    uart_putc_early('\r');
    uart_putc_early('\n');
}

// Clear screen for a fresh start
void uart_clear_screen(void) {
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART_PHYS;
    
    // Send many newlines to clear past output
    for (int i = 0; i < 50; i++) {
        *UART0_DR = '\r';
        *UART0_DR = '\n';
    }
    
    // Send a clear header
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = ' ';
    *UART0_DR = 'C';
    *UART0_DR = 'L';
    *UART0_DR = 'E';
    *UART0_DR = 'A';
    *UART0_DR = 'R';
    *UART0_DR = ' ';
    *UART0_DR = 'O';
    *UART0_DR = 'U';
    *UART0_DR = 'T';
    *UART0_DR = 'P';
    *UART0_DR = 'U';
    *UART0_DR = 'T';
    *UART0_DR = ' ';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '=';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    *UART0_DR = '\r';
    *UART0_DR = '\n';
    
    // Delay to ensure UART sends everything
    for (volatile int i = 0; i < 25000; i++) {
        // Delay
    }
} 