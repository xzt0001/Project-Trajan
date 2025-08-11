/*
 * panic.c - Kernel panic and emergency stop functionality
 * 
 * This module provides emergency system halt capabilities with basic
 * diagnostic output. Uses direct UART access to ensure output even
 * when other subsystems have failed.
 */

#include "../include/panic.h"

// Platform-specific UART base address
// TODO: Move to platform configuration header
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

/**
 * panic - Emergency system halt with diagnostic message
 * @message: Error message to display before halting
 * 
 * Outputs a panic message directly to UART and halts the system.
 * This function never returns and is safe to call from any context,
 * including interrupt handlers and early boot code.
 * 
 * Uses direct UART register access to bypass any potentially
 * corrupted driver state.
 */
void panic(const char* message) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    // Output panic header
    *uart = 'P'; *uart = 'A'; *uart = 'N'; *uart = 'I'; *uart = 'C'; 
    *uart = ':'; *uart = ' ';
    
    // Output message if provided
    if (message) {
        while (*message) {
            *uart = *message++;
        }
    }
    
    // Terminate with newline
    *uart = '\r'; *uart = '\n';
    
    // Hang indefinitely - system is in unrecoverable state
    while (1) {
        // Infinite loop - system cannot continue
    }
}
