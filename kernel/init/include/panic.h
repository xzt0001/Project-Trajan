/*
 * panic.h - Kernel panic and emergency stop API
 * 
 * Provides emergency system halt functionality for unrecoverable errors.
 * Safe to call from any context including interrupt handlers and early boot.
 */

#ifndef KERNEL_INIT_PANIC_H
#define KERNEL_INIT_PANIC_H

#include "../../../include/types.h"

/**
 * panic - Emergency system halt with diagnostic message
 * @message: Error message to display before halting (can be NULL)
 * 
 * This function never returns. It outputs the message directly to UART
 * and halts the system in an infinite loop. Safe to call from any context.
 * 
 * Example usage:
 *   panic("Memory initialization failed");
 *   panic(NULL);  // Silent panic
 */
void panic(const char* message) __attribute__((noreturn));

#endif /* KERNEL_INIT_PANIC_H */
