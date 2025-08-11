/*
 * console_api.h - Kernel console API
 * 
 * Provides unified console interface for both early (pre-MMU) and late
 * (post-MMU) kernel console operations. Abstracts away the underlying
 * implementation details and provides a clean API for kernel modules.
 */

#ifndef KERNEL_INIT_CONSOLE_API_H
#define KERNEL_INIT_CONSOLE_API_H

#include "../../../include/types.h"

/* ========== Early Console API (Pre-MMU) ========== */

/**
 * early_console_print - Print string with newline conversion
 * @msg: Null-terminated string to print
 * 
 * Safe to call before MMU initialization. Performs LF->CRLF conversion.
 * Uses direct UART register access.
 */
void early_console_print(const char* msg);

/**
 * early_console_hex64 - Print labeled 64-bit hex value
 * @label: Label to print before hex value (can be NULL or empty)
 * @value: 64-bit value to print in hexadecimal
 * 
 * Format: "<label>0x<16-hex-digits>\n"
 * Safe to call before MMU initialization.
 */
void early_console_hex64(const char* label, uint64_t value);

/**
 * early_console_putc - Output single character
 * @c: Character to output
 * 
 * Raw character output without any formatting.
 * Safe to call before MMU initialization.
 */
void early_console_putc(char c);

/**
 * early_console_puts - Output string without formatting
 * @str: Null-terminated string to output
 * 
 * Raw string output without newline conversion.
 * Safe to call before MMU initialization.
 */
void early_console_puts(const char* str);

/* ========== Legacy Compatibility Functions ========== */

/**
 * Legacy function names for compatibility with existing code.
 * These are implemented as actual functions in early_console.c
 * to maintain compatibility with existing kernel modules.
 */
void debug_print(const char* msg);
void debug_hex64(const char* label, uint64_t value);

/* Convenience aliases for write functions */
#define write_uart      early_console_putc
#define write_string    early_console_puts

/* ========== Future: Late Console API (Post-MMU) ========== */

/*
 * TODO: Add post-MMU console functions:
 * - klog_print()    - Virtual memory aware logging
 * - klog_hex64()    - Virtual memory aware hex output
 * - klog_init()     - Initialize post-MMU console
 * - klog_level()    - Set logging verbosity level
 */

#endif /* KERNEL_INIT_CONSOLE_API_H */
