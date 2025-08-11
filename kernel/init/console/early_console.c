/*
 * early_console.c - Early kernel console (pre-MMU)
 * 
 * Provides direct UART access for kernel debugging before virtual memory
 * is enabled. Uses raw hardware register access to ensure output even
 * when other subsystems have failed.
 */

#include "../include/console_api.h"

// UART register definitions (TODO: move to platform config)
#define UART0_BASE_ADDR 0x09000000
#define UART_DR_REG     (UART0_BASE_ADDR + 0x00)
#define UART_FR_REG     (UART0_BASE_ADDR + 0x18)
#define UART_FR_TXFF    (1 << 5)

// Global UART register for direct access
volatile unsigned int* const GLOBAL_UART __attribute__((used)) = (volatile unsigned int*)0x09000000;

/**
 * early_console_print - Print string to early console
 * @msg: Null-terminated string to print
 * 
 * Outputs a string directly to UART using raw register access.
 * Handles newline conversion (LF -> CRLF) for proper terminal display.
 * Safe to call before MMU initialization.
 */
void early_console_print(const char* msg) {
    // Pointer to UART Data Register (where we write characters)
    volatile uint32_t* dr = (volatile uint32_t*)UART_DR_REG;
    // Pointer to UART Flag Register (status checks)
    volatile uint32_t* fr = (volatile uint32_t*)UART_FR_REG;
    
    // Safety check for null pointer
    if (!msg) return;
    
    // Loop through each character in the message
    while (*msg) {
        // Handle newline characters by adding carriage return
        if (*msg == '\n') {
            // Wait until TX FIFO has space (bit 5 = TXFF flag)
            while ((*fr) & UART_FR_TXFF);
            // Send carriage return before newline
            *dr = '\r';
        }
        
        // Wait for TX FIFO to have space again
        while ((*fr) & UART_FR_TXFF);
        // Send current character and move to next
        *dr = *msg++;
    }
}

/**
 * early_console_hex64 - Print 64-bit value in hexadecimal
 * @label: Optional label to print before the hex value
 * @value: 64-bit value to print in hex
 * 
 * Outputs a labeled 64-bit hexadecimal value to early console.
 * Format: "<label>0x<16-digit-hex>\n"
 */
void early_console_hex64(const char* label, uint64_t value) {
    // Print the label first
    early_console_print(label);
    
    // Direct UART access for raw hex output
    volatile uint32_t* dr = (volatile uint32_t*)UART_DR_REG;
    volatile uint32_t* fr = (volatile uint32_t*)UART_FR_REG;
    
    // Output "0x" prefix
    while ((*fr) & UART_FR_TXFF);
    *dr = '0';
    while ((*fr) & UART_FR_TXFF);
    *dr = 'x';
    
    // Print all 16 hex digits (64 bits)
    for (int i = 15; i >= 0; i--) {
        // Extract each hex digit
        int digit = (value >> (i * 4)) & 0xF;
        char hex_char = digit < 10 ? ('0' + digit) : ('A' + (digit - 10));
        
        // Output to UART
        while ((*fr) & UART_FR_TXFF);
        *dr = hex_char;
    }
    
    // Add newline
    while ((*fr) & UART_FR_TXFF);
    *dr = '\r';
    while ((*fr) & UART_FR_TXFF);
    *dr = '\n';
}

/**
 * early_console_putc - Output single character
 * @c: Character to output
 * 
 * Outputs a single character to early console using direct UART access.
 * Used for simple character output and as building block for other functions.
 */
void early_console_putc(char c) {
    *GLOBAL_UART = c;
}

/**
 * early_console_puts - Output string without newline conversion
 * @str: Null-terminated string to output
 * 
 * Outputs a string character by character without any formatting.
 * Does not perform newline conversion like early_console_print().
 */
void early_console_puts(const char* str) {
    while (*str) {
        early_console_putc(*str++);
    }
}

/* ========== Legacy Compatibility Functions ========== */

/**
 * debug_print - Legacy compatibility wrapper
 * @msg: Message to print
 * 
 * Provides backward compatibility for existing kernel code
 * that calls debug_print() directly.
 */
void debug_print(const char* msg) {
    early_console_print(msg);
}

/**
 * debug_hex64 - Legacy compatibility wrapper  
 * @label: Label for the hex value
 * @value: 64-bit value to print
 * 
 * Provides backward compatibility for existing kernel code
 * that calls debug_hex64() directly.
 */
void debug_hex64(const char* label, uint64_t value) {
    early_console_hex64(label, value);
}
