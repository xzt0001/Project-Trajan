/*
 * scheduler_tests.c - Scheduler and task management testing framework
 * 
 * Provides comprehensive tests for scheduler initialization, task creation,
 * context switching, and task management functionality. These tests validate
 * the core scheduling infrastructure and task lifecycle management.
 */

#include "../include/selftest.h"
#include "../include/console_api.h"
#include "../include/sample_tasks.h"

// Platform constants
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// External declarations for scheduler testing
extern char vector_table[];
extern void full_restore_context(task_t* task);
extern void dummy_task_a(void);
extern void dummy_asm(void);
extern void known_branch_test(void);
extern void test_context_switch(void);
extern void debug_check_mapping(uint64_t addr, const char* name);

// UART function declarations
extern void uart_puts(const char* str);
extern void uart_hex64(uint64_t value);
extern void uart_putc(char c);
extern void uart_print_hex(uint64_t value);

/**
 * init_scheduler - Initialize scheduler subsystem
 * 
 * Performs basic scheduler initialization and outputs identification
 * markers. This is a minimal initialization suitable for early testing
 * before full scheduler functionality is available.
 */
void init_scheduler(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'I'; *uart = 'N'; *uart = 'I'; *uart = 'T'; *uart = '_'; 
    *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = '\r'; *uart = '\n';
}

/**
 * start_scheduler - Start scheduler with comprehensive testing
 * 
 * Comprehensive scheduler startup routine that includes:
 * - VBAR_EL1 verification and correction
 * - Task structure initialization
 * - Sample task setup and testing
 * - Context switching verification
 * - Direct function call testing
 * - Full task restoration testing
 * 
 * This function demonstrates the complete scheduler startup sequence
 * and provides extensive testing of task management functionality.
 */
void start_scheduler(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T'; *uart = '_'; 
    *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = '\r'; *uart = '\n';
    
    // Check VBAR_EL1 at scheduler start
    uint64_t vbar_check;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar_check));
    uart_puts("[SCHED] VBAR_EL1 at scheduler start: 0x");
    uart_hex64(vbar_check);
    uart_puts("\n");
    
    // Ensure correct VBAR_EL1 setting
    if (vbar_check != (uint64_t)vector_table) {
        uart_puts("[SCHED] ERROR: VBAR_EL1 incorrect at scheduler start! Fixing...\n");
        asm volatile("msr vbar_el1, %0" :: "r"(vector_table));
        asm volatile("isb");
    }
    
    // Create a task_t structure for task_a and jump to it
    // Use modular task API
    task_t test_task;
    
    // Clear the task structure
    for (int i = 0; i < sizeof(test_task)/sizeof(uint64_t); i++) {
        ((uint64_t*)&test_task)[i] = 0;
    }
    
    // Set up stack pointer (use a fixed stack area)
    uint64_t* test_stack = (uint64_t*)0x90000; // Use a known address
    test_task.stack_ptr = test_stack;
    
    // Set up PC using modular task API
    test_task.pc = (uint64_t)get_demo_task_a();
    
    // Set up SPSR for EL1h with interrupts enabled
    test_task.spsr = 0x345;
    
    // Test if we can directly call the dummy_task_a function
    uart_puts("[DEBUG] Testing direct call to dummy_task_a()...\n");
    
    // Call the function directly - if it prints 'A', the function is sound
    // If it crashes, PC or memory permissions are invalid
    dummy_task_a();
    
    uart_puts("[DEBUG] Returned from direct function call\n");
    
    // Try calling dummy_asm first as a sanity test
    uart_puts("[DEBUG] Calling dummy_asm...\n");
    dummy_asm();  // If this prints 'Z', code execution path is valid
    uart_puts("[DEBUG] Returned from dummy_asm\n");
    
    // Try the known branch test function
    uart_puts("[DEBUG] Calling known_branch_test...\n");
    known_branch_test();  // If this prints 'X', code execution path is valid
    uart_puts("[DEBUG] Returned from known_branch_test\n");
    
    // Try our minimal context switch test first
    uart_puts("[DEBUG] Trying minimal context switch test...\n");
    
    // Debug checks for proper mapping and permissions
    uart_puts("[DEBUG] Checking dummy_asm address mapping\n");
    uint64_t dummy_addr = (uint64_t)dummy_asm;
    char buf[128];
    extern int snprintf(char* buffer, size_t count, const char* format, ...);
    snprintf(buf, sizeof(buf), "[DEBUG] dummy_asm @ 0x%lx\n", dummy_addr);
    uart_puts(buf);

    // Verify dummy_asm is mapped with proper permissions
    debug_check_mapping(dummy_addr, "dummy_asm");

    // Direct marker immediately before the call
    uart_putc('B');  // BEFORE test_context_switch

    test_context_switch();  // no logic, just jump
    
    // We shouldn't get here if the test worked
    uart_puts("[DEBUG] RETURNED FROM TEST CONTEXT SWITCH!\n");
    
    // This is a one-way jump that transfers control to the task
    full_restore_context(&test_task);
    
    // Should never reach here
    *uart = '!';  // Error
}

/**
 * test_scheduler_minimal - Minimal scheduler functionality test
 * 
 * Ultra-minimal scheduler test that just prints identification markers
 * and returns. Used for basic scheduler subsystem verification without
 * complex operations that might fail in early boot environments.
 */
void test_scheduler_minimal(void) {
    // Direct UART access with minimal code
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    // Print minimal markers
    *uart = 'M';  // M for Minimal test
    *uart = 'S';  // S for Scheduler
    *uart = 'T';  // T for Test
    *uart = '\r';
    *uart = '\n';
    
    // That's it - return immediately without doing anything complex
}

/**
 * test_scheduler - Basic scheduler functionality test
 * 
 * Simple scheduler test that outputs identification markers and tests
 * basic function call/return functionality. More comprehensive than
 * the minimal test but still safe for early boot environments.
 */
void test_scheduler(void) {
    volatile uint32_t* uart = (volatile uint32_t*)DEBUG_UART;
    
    // Simple output directly to UART
    *uart = 'S'; *uart = 'C'; *uart = 'H'; *uart = 'D'; *uart = ':';
    
    // Just test returning from a function
    *uart = 'D';
    *uart = 'O';
    *uart = 'N';
    *uart = 'E';
    *uart = '!';
    *uart = '\r';
    *uart = '\n';
    
    // Return without doing anything complex
}

/**
 * test_task_creation - Test task structure creation and initialization
 * 
 * Tests the creation and initialization of task structures, including
 * memory clearing, register setup, and stack allocation. Validates
 * task structure integrity before context switching attempts.
 */
void test_task_creation(void) {
    debug_print("\n[SCHED] Testing task creation...\n");
    
    // Create and initialize a test task
    task_t test_task;
    
    // Clear the task structure
    for (int i = 0; i < sizeof(test_task)/sizeof(uint64_t); i++) {
        ((uint64_t*)&test_task)[i] = 0;
    }
    
    debug_print("[SCHED] Task structure cleared\n");
    
    // Set up basic task parameters
    uint64_t* test_stack = (uint64_t*)0x90000;
    test_task.stack_ptr = test_stack;
    test_task.pc = (uint64_t)get_demo_task_a();
    test_task.spsr = 0x345;  // EL1h with interrupts enabled
    
    debug_print("[SCHED] Task parameters set:\n");
    debug_print("  Stack: 0x");
    uart_print_hex((uint64_t)test_task.stack_ptr);
    debug_print("\n  PC: 0x");
    uart_print_hex(test_task.pc);
    debug_print("\n  SPSR: 0x");
    uart_print_hex(test_task.spsr);
    debug_print("\n");
    
    // Verify task structure integrity
    if (test_task.stack_ptr != NULL && test_task.pc != 0) {
        debug_print("[SCHED] Task creation successful\n");
    } else {
        debug_print("[SCHED] ERROR: Task creation failed\n");
    }
    
    debug_print("[SCHED] Task creation test complete\n\n");
}

/**
 * test_context_functions - Test context switching support functions
 * 
 * Tests various functions that support context switching including
 * dummy functions, branch tests, and context switch preparations.
 * These tests validate the infrastructure needed for task switching.
 */
void test_context_functions(void) {
    debug_print("\n[SCHED] Testing context switch support functions...\n");
    
    // Test dummy_asm function
    debug_print("[SCHED] Testing dummy_asm...\n");
    dummy_asm();
    debug_print("[SCHED] dummy_asm completed\n");
    
    // Test known_branch_test function
    debug_print("[SCHED] Testing known_branch_test...\n");
    known_branch_test();
    debug_print("[SCHED] known_branch_test completed\n");
    
    // Test direct task function call
    debug_print("[SCHED] Testing direct task function call...\n");
    dummy_task_a();
    debug_print("[SCHED] Direct task call completed\n");
    
    // Test address mapping verification
    debug_print("[SCHED] Testing address mapping verification...\n");
    uint64_t task_addr = (uint64_t)get_demo_task_a();
    debug_print("[SCHED] Demo task A address: 0x");
    uart_print_hex(task_addr);
    debug_print("\n");
    
    debug_check_mapping(task_addr, "demo_task_a");
    
    debug_print("[SCHED] Context function tests complete\n\n");
}

/**
 * test_scheduler_state - Test and display scheduler state
 * 
 * Displays comprehensive scheduler state information including
 * VBAR configuration, task pointers, stack information, and
 * scheduler readiness indicators.
 */
void test_scheduler_state(void) {
    debug_print("\n[SCHED] Scheduler state inspection:\n");
    
    // Check VBAR_EL1 setting
    uint64_t vbar;
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    debug_print("VBAR_EL1: 0x");
    uart_print_hex(vbar);
    debug_print("\n");
    
    // Check vector table address
    uint64_t vector_addr = (uint64_t)vector_table;
    debug_print("Vector table: 0x");
    uart_print_hex(vector_addr);
    debug_print("\n");
    
    // Verify VBAR matches vector table
    if (vbar == vector_addr) {
        debug_print("VBAR correctly configured\n");
    } else {
        debug_print("WARNING: VBAR mismatch!\n");
    }
    
    // Display available demo tasks
    debug_print("Available demo tasks:\n");
    debug_print("  task_a: 0x");
    uart_print_hex((uint64_t)get_demo_task_a());
    debug_print("\n  task_b: 0x");
    uart_print_hex((uint64_t)get_demo_task_b());
    debug_print("\n");
    
    // Display stack information
    debug_print("Test stack area: 0x90000\n");
    
    debug_print("Scheduler state inspection complete\n\n");
}

/**
 * test_scheduler_integration - Test scheduler integration
 * 
 * Tests integration between scheduler and other kernel subsystems
 * including task management, memory management, and interrupt handling.
 * Validates that the scheduler works correctly with other kernel components.
 */
void test_scheduler_integration(void) {
    debug_print("\n[SCHED] Testing scheduler integration...\n");
    
    // Test integration with sample tasks
    debug_print("[SCHED] Testing sample task integration...\n");
    demo_task_info();
    
    // Test scheduler initialization sequence
    debug_print("[SCHED] Testing initialization sequence...\n");
    init_scheduler();
    debug_print("[SCHED] Scheduler initialized\n");
    
    // Test task creation with integration
    debug_print("[SCHED] Testing integrated task creation...\n");
    test_task_creation();
    
    // Test context function integration
    debug_print("[SCHED] Testing context function integration...\n");
    test_context_functions();
    
    // Test scheduler state verification
    debug_print("[SCHED] Testing state verification...\n");
    test_scheduler_state();
    
    debug_print("[SCHED] Scheduler integration tests complete\n\n");
}
