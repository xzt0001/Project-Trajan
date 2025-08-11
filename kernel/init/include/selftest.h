/*
 * selftest.h - Self-test framework API for kernel validation
 * 
 * Provides a comprehensive self-test framework for validating kernel
 * functionality including exception handling, UART communication,
 * scheduler operation, and system state verification. These tests
 * are designed to be run during kernel initialization to verify
 * proper system configuration and functionality.
 */

#ifndef KERNEL_INIT_SELFTEST_H
#define KERNEL_INIT_SELFTEST_H

#include "../../../include/types.h"
#include "../../../include/task.h"

/* ========== Exception Testing Functions ========== */

/**
 * test_exception_delivery - Comprehensive exception system validation
 * 
 * Performs extensive testing of the exception handling system including:
 * - CPU state inspection (EL, DAIF, VBAR, SCTLR)
 * - SVC exception generation and handling
 * - Timer interrupt testing
 * - Interrupt enable/disable verification
 * - IRQ handler direct invocation
 * - Vector table mapping validation
 * 
 * This is a critical test that validates the core exception infrastructure
 * required for proper kernel operation.
 */
void test_exception_delivery(void);

/**
 * test_exception_handling - Basic exception handling verification
 * 
 * Performs simple SVC exception tests to verify basic exception handling
 * functionality. This is a lighter-weight test suitable for post-MMU
 * environments where full debug infrastructure is available.
 */
void test_exception_handling(void);

/**
 * test_system_state - Display current system state for debugging
 * 
 * Outputs comprehensive system state information including CPU registers,
 * MMU status, interrupt state, and exception configuration. Useful for
 * debugging exception and system configuration issues.
 */
void test_system_state(void);

/**
 * test_svc_variants - Test different SVC instruction variants
 * 
 * Tests various SVC immediate values to ensure the SVC handler
 * correctly processes different system call numbers. Useful for
 * validating syscall dispatch logic.
 */
void test_svc_variants(void);

/* ========== UART Testing Functions ========== */

/**
 * test_uart_direct - Direct UART register access test
 * 
 * Tests the most basic UART functionality by writing directly to the
 * UART data register. This test uses minimal dependencies and should
 * work even in early boot environments or when other UART functions
 * are not available.
 */
void test_uart_direct(void);

/**
 * test_uart_after_mmu - UART functionality test after MMU enablement
 * 
 * Comprehensive UART testing for post-MMU environments. Tests string
 * output, hex formatting, direct register access with virtual addressing,
 * and various UART utility functions. Critical for validating UART
 * functionality after memory management transitions.
 */
void test_uart_after_mmu(void);

/**
 * test_uart_character_set - Test UART with various character types
 * 
 * Tests UART output with different character types including printable
 * ASCII, control characters, and special symbols. Useful for verifying
 * UART character handling and terminal compatibility.
 */
void test_uart_character_set(void);

/**
 * test_uart_timing - UART timing and throughput test
 * 
 * Tests UART output timing by sending known patterns and measuring
 * delays. Useful for verifying UART baud rate and timing characteristics.
 * Also tests UART behavior under different output loads.
 */
void test_uart_timing(void);

/**
 * test_uart_hex_formatting - Test hex number formatting functions
 * 
 * Tests various hex formatting functions with different value ranges
 * and formats. Validates that hex output is correctly formatted and
 * readable for debugging purposes.
 */
void test_uart_hex_formatting(void);

/**
 * test_uart_string_functions - Test string output functions
 * 
 * Tests various string output functions including early console,
 * late console, and debug print functions. Validates string
 * handling across different system states.
 */
void test_uart_string_functions(void);

/**
 * test_uart_error_conditions - Test UART error handling
 * 
 * Tests UART behavior under various error conditions and edge cases.
 * Validates that UART functions handle unusual inputs gracefully.
 */
void test_uart_error_conditions(void);

/* ========== Scheduler Testing Functions ========== */

/**
 * init_scheduler - Initialize scheduler subsystem
 * 
 * Performs basic scheduler initialization and outputs identification
 * markers. This is a minimal initialization suitable for early testing
 * before full scheduler functionality is available.
 */
void init_scheduler(void);

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
void start_scheduler(void);

/**
 * test_scheduler_minimal - Minimal scheduler functionality test
 * 
 * Ultra-minimal scheduler test that just prints identification markers
 * and returns. Used for basic scheduler subsystem verification without
 * complex operations that might fail in early boot environments.
 */
void test_scheduler_minimal(void);

/**
 * test_scheduler - Basic scheduler functionality test
 * 
 * Simple scheduler test that outputs identification markers and tests
 * basic function call/return functionality. More comprehensive than
 * the minimal test but still safe for early boot environments.
 */
void test_scheduler(void);

/**
 * test_task_creation - Test task structure creation and initialization
 * 
 * Tests the creation and initialization of task structures, including
 * memory clearing, register setup, and stack allocation. Validates
 * task structure integrity before context switching attempts.
 */
void test_task_creation(void);

/**
 * test_context_functions - Test context switching support functions
 * 
 * Tests various functions that support context switching including
 * dummy functions, branch tests, and context switch preparations.
 * These tests validate the infrastructure needed for task switching.
 */
void test_context_functions(void);

/**
 * test_scheduler_state - Test and display scheduler state
 * 
 * Displays comprehensive scheduler state information including
 * VBAR configuration, task pointers, stack information, and
 * scheduler readiness indicators.
 */
void test_scheduler_state(void);

/**
 * test_scheduler_integration - Test scheduler integration
 * 
 * Tests integration between scheduler and other kernel subsystems
 * including task management, memory management, and interrupt handling.
 * Validates that the scheduler works correctly with other kernel components.
 */
void test_scheduler_integration(void);

/* ========== Comprehensive Test Suites ========== */

/**
 * run_exception_test_suite - Run complete exception test suite
 * 
 * Executes all exception-related tests in a logical sequence.
 * Includes system state inspection, exception delivery testing,
 * and SVC variant testing.
 */
static inline void run_exception_test_suite(void) {
    test_system_state();
    test_exception_delivery();
    test_svc_variants();
    test_exception_handling();
}

/**
 * run_uart_test_suite - Run complete UART test suite
 * 
 * Executes all UART-related tests in a logical sequence.
 * Includes direct access, string functions, character sets,
 * timing, formatting, and error condition testing.
 */
static inline void run_uart_test_suite(void) {
    test_uart_direct();
    test_uart_character_set();
    test_uart_string_functions();
    test_uart_hex_formatting();
    test_uart_timing();
    test_uart_error_conditions();
}

/**
 * run_scheduler_test_suite - Run complete scheduler test suite
 * 
 * Executes all scheduler-related tests in a logical sequence.
 * Includes initialization, task creation, context functions,
 * state verification, and integration testing.
 */
static inline void run_scheduler_test_suite(void) {
    test_scheduler_minimal();
    test_scheduler();
    test_task_creation();
    test_context_functions();
    test_scheduler_state();
    test_scheduler_integration();
}

/**
 * run_post_mmu_test_suite - Run tests suitable for post-MMU environment
 * 
 * Executes tests that are specifically designed for or enhanced in
 * post-MMU environments where virtual addressing is available.
 */
static inline void run_post_mmu_test_suite(void) {
    test_uart_after_mmu();
    test_exception_handling();
    test_scheduler_integration();
}

/* ========== Test Configuration and Constants ========== */

// Test configuration flags
#define SELFTEST_ENABLE_EXCEPTION_TESTS    1
#define SELFTEST_ENABLE_UART_TESTS         1
#define SELFTEST_ENABLE_SCHEDULER_TESTS    1
#define SELFTEST_ENABLE_COMPREHENSIVE_TESTS 1

// Test timing constants
#define SELFTEST_DELAY_SHORT    10000
#define SELFTEST_DELAY_MEDIUM   50000
#define SELFTEST_DELAY_LONG     100000

// Test data patterns
#define SELFTEST_PATTERN_A      0xAAAAAAAA
#define SELFTEST_PATTERN_5      0x55555555
#define SELFTEST_PATTERN_F      0xFFFFFFFF
#define SELFTEST_PATTERN_0      0x00000000

/* ========== Test Result Tracking ========== */

typedef struct {
    bool exception_tests_passed;
    bool uart_tests_passed;
    bool scheduler_tests_passed;
    uint32_t tests_run;
    uint32_t tests_passed;
    uint32_t tests_failed;
} selftest_results_t;

// Global test results (if needed)
extern selftest_results_t g_selftest_results;

/* ========== Integration Notes ========== */

/*
 * Usage in kernel_main:
 * 
 * // Early boot tests (pre-MMU)
 * test_uart_direct();
 * test_system_state();
 * 
 * // Post-MMU tests
 * if (mmu_enabled) {
 *     run_post_mmu_test_suite();
 * }
 * 
 * // Full test suite (if time/space permits)
 * if (SELFTEST_ENABLE_COMPREHENSIVE_TESTS) {
 *     run_exception_test_suite();
 *     run_uart_test_suite();
 *     run_scheduler_test_suite();
 * }
 */

#endif /* KERNEL_INIT_SELFTEST_H */
