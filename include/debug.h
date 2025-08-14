#ifndef DEBUG_H
#define DEBUG_H

#include "uart.h"
#include "types.h"

#define DEBUG_SCHED 1

#if DEBUG_SCHED
#define dbg_uart(msg) uart_puts(msg)
#else
#define dbg_uart(msg)
#endif

/* ========== Console Debug Functions ========== */
/* These functions are provided by the init console module */

/**
 * debug_print - Print string to early console
 * @msg: Null-terminated string to print
 * 
 * Compatible with legacy debug_print calls throughout the kernel.
 * Uses early console implementation for robust output.
 */
void debug_print(const char* msg);

/**
 * debug_hex64 - Print labeled 64-bit hex value  
 * @label: Label to print before hex value
 * @value: 64-bit value to print in hexadecimal
 * 
 * Compatible with legacy debug_hex64 calls throughout the kernel.
 * Uses early console implementation for robust output.
 */
void debug_hex64(const char* label, uint64_t value);

#endif /* DEBUG_H */ 