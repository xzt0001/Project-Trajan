/*
 * sample_tasks.h - Sample task API for demonstration and testing
 * 
 * Provides declarations and utilities for sample tasks used to demonstrate
 * scheduler functionality, context switching, and basic task behavior.
 * These tasks are designed to be simple, reliable, and highly visible.
 */

#ifndef KERNEL_INIT_SAMPLE_TASKS_H
#define KERNEL_INIT_SAMPLE_TASKS_H

#include "../../../include/types.h"

/* ========== Demo Task Functions ========== */

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

/* ========== Task Access Functions ========== */

/**
 * get_demo_task_a - Get function pointer for demo task A
 * @return: Function pointer to task_a
 * 
 * Returns a function pointer to task_a for use by the scheduler
 * and task management systems. This provides a clean interface
 * for referencing the task without external declarations.
 */
void (*get_demo_task_a(void))(void);

/**
 * get_demo_task_b - Get function pointer for demo task B
 * @return: Function pointer to task_b
 * 
 * Returns a function pointer to task_b for use by the scheduler
 * and task management systems. This provides a clean interface
 * for referencing the task without external declarations.
 */
void (*get_demo_task_b(void))(void);

/* ========== Utility Functions ========== */

/**
 * demo_task_info - Display information about demo tasks
 * 
 * Outputs information about the available demo tasks to help
 * with debugging and system verification. Uses early console
 * output to ensure visibility even during early boot.
 */
void demo_task_info(void);

/* ========== Task Configuration ========== */

// Platform constants for task configuration
#ifndef DEMO_UART_BASE
#define DEMO_UART_BASE 0x09000000  // Default UART base address
#endif

#define TASK_DELAY_LOOPS 100000     // Default delay loop count

/* ========== Task Attributes ========== */

// Common task attributes for consistent behavior
#define DEMO_TASK_ATTRIBUTES \
    __attribute__((noreturn, used, externally_visible, section(".text")))

/* ========== Future Extensions ========== */

/*
 * TODO: Additional sample tasks that could be added:
 * - task_timer() - Task that responds to timer interrupts
 * - task_counter() - Task that maintains a counter and displays it
 * - task_memory() - Task that exercises memory allocation
 * - task_syscall() - Task that demonstrates system call usage
 * - task_interrupt() - Task that handles specific interrupt types
 * - task_cooperative() - Task that yields voluntarily
 * - task_priority() - Tasks with different priority levels
 */

/* ========== Integration Notes ========== */

/*
 * Usage in scheduler:
 * 
 * // Get task function pointers
 * void (*task_func_a)(void) = get_demo_task_a();
 * void (*task_func_b)(void) = get_demo_task_b();
 * 
 * // Create task structures and add to scheduler
 * create_task(task_func_a);
 * create_task(task_func_b);
 * 
 * // Or direct reference for testing
 * extern void task_a(void);
 * test_task.pc = (uint64_t)task_a;
 */

#endif /* KERNEL_INIT_SAMPLE_TASKS_H */
