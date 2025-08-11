/*
 * demo_tasks.c - Demonstration tasks for testing scheduler and context switching
 * 
 * Provides simple example tasks that demonstrate basic task behavior and can be
 * used to test the scheduler, context switching, and task management systems.
 * These tasks use direct UART output for maximum visibility and minimal dependencies.
 */

#include "../include/sample_tasks.h"

// Platform constants (TODO: move to platform config)
#ifndef DEMO_UART_BASE
#define DEMO_UART_BASE 0x09000000
#endif

// Delay constant for task output throttling
#define TASK_DELAY_LOOPS 100000

/**
 * task_a - Demonstration task A
 * 
 * A simple task that continuously outputs 'A' characters to the UART.
 * Uses direct UART access for maximum reliability and minimal overhead.
 * Includes startup markers and delay loops to make output visible.
 * 
 * This task never returns - it runs in an infinite loop until preempted
 * by the scheduler or system shutdown.
 * 
 * Attributes:
 * - noreturn: Indicates this function never returns
 * - used: Prevents the compiler from optimizing it away
 * - externally_visible: Ensures the symbol is visible to the linker
 * - section(".text"): Places the function in the standard text section
 */
void task_a(void) __attribute__((noreturn, used, externally_visible, section(".text")));
void task_a(void) {
    // Direct UART access for immediate output visibility
    volatile uint32_t* uart = (volatile uint32_t*)DEMO_UART_BASE;
    
    // Startup sequence - clear identification of task entry
    *uart = 'A'; 
    *uart = '_';
    *uart = 'S';
    *uart = 'T';
    *uart = 'A';
    *uart = 'R';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    // Main task loop - continuous operation
    while (1) {
        // Output task identifier
        *uart = 'A';
        
        // Throttling delay to prevent UART flooding
        // Uses volatile to prevent compiler optimization
        for (volatile int i = 0; i < TASK_DELAY_LOOPS; i++) {
            // Intentionally empty - pure delay loop
        }
    }
}

/**
 * task_b - Demonstration task B
 * 
 * A simple task that continuously outputs 'B' characters to the UART.
 * Similar structure to task_a but with different output character.
 * Used to demonstrate task switching and concurrent execution.
 * 
 * This task never returns - it runs in an infinite loop until preempted
 * by the scheduler or system shutdown.
 * 
 * Attributes:
 * - noreturn: Indicates this function never returns
 * - used: Prevents the compiler from optimizing it away
 * - externally_visible: Ensures the symbol is visible to the linker
 * - section(".text"): Places the function in the standard text section
 */
void task_b(void) __attribute__((noreturn, used, externally_visible, section(".text")));
void task_b(void) {
    // Direct UART access for immediate output visibility
    volatile uint32_t* uart = (volatile uint32_t*)DEMO_UART_BASE;
    
    // Startup sequence - clear identification of task entry
    *uart = 'B'; 
    *uart = '_';
    *uart = 'S';
    *uart = 'T';
    *uart = 'A';
    *uart = 'R';
    *uart = 'T';
    *uart = '\r';
    *uart = '\n';
    
    // Main task loop - continuous operation
    while (1) {
        // Output task identifier
        *uart = 'B';
        
        // Throttling delay to prevent UART flooding
        // Uses volatile to prevent compiler optimization
        for (volatile int i = 0; i < TASK_DELAY_LOOPS; i++) {
            // Intentionally empty - pure delay loop
        }
    }
}

/**
 * get_demo_task_a - Get function pointer for demo task A
 * @return: Function pointer to task_a
 * 
 * Returns a function pointer to task_a for use by the scheduler
 * and task management systems. This provides a clean interface
 * for referencing the task without external declarations.
 */
void (*get_demo_task_a(void))(void) {
    return task_a;
}

/**
 * get_demo_task_b - Get function pointer for demo task B
 * @return: Function pointer to task_b
 * 
 * Returns a function pointer to task_b for use by the scheduler
 * and task management systems. This provides a clean interface
 * for referencing the task without external declarations.
 */
void (*get_demo_task_b(void))(void) {
    return task_b;
}

/**
 * demo_task_info - Display information about demo tasks
 * 
 * Outputs information about the available demo tasks to help
 * with debugging and system verification. Uses early console
 * output to ensure visibility even during early boot.
 */
void demo_task_info(void) {
    // Use early console for output (compatible with both pre/post MMU)
    extern void early_console_print(const char* msg);
    
    early_console_print("\n=== Demo Tasks Available ===\n");
    early_console_print("task_a: Outputs 'A' continuously\n");
    early_console_print("task_b: Outputs 'B' continuously\n");
    early_console_print("Both tasks use direct UART output\n");
    early_console_print("Delay: ");
    
    // Convert delay constant to string manually (no printf available)
    volatile uint32_t* uart = (volatile uint32_t*)DEMO_UART_BASE;
    int delay = TASK_DELAY_LOOPS;
    
    // Simple decimal output (for small numbers)
    if (delay >= 100000) {
        *uart = '1'; *uart = '0'; *uart = '0'; *uart = '0'; *uart = '0'; *uart = '0';
    } else if (delay >= 10000) {
        *uart = '1'; *uart = '0'; *uart = '0'; *uart = '0'; *uart = '0';
    } else if (delay >= 1000) {
        *uart = '1'; *uart = '0'; *uart = '0'; *uart = '0';
    }
    
    early_console_print(" loops\n");
    early_console_print("===========================\n\n");
}
