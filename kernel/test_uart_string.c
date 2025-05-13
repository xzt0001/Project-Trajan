#include "../include/uart.h"
#include "../include/types.h"

// This file contains test functions for UART string output validation

// Test function to validate string output pre-MMU
void test_uart_string_early(void) {
    // Static string defined at compile time
    const char *static_str = "Static string defined at compile time\n";
    
    // Use early UART functions explicitly
    uart_puts_early("[TEST] Testing UART string output (pre-MMU)\n");
    uart_puts_early("[TEST] Static string: ");
    uart_puts_early(static_str);
    
    // Try array-based string
    char arr_str[] = "Array-based string (stack)\n";
    uart_puts_early("[TEST] Array string: ");
    uart_puts_early(arr_str);
    
    // Direct string literal
    uart_puts_early("[TEST] Direct literal: Hello from direct string literal\n");
    
    uart_puts_early("[TEST] Pre-MMU UART test completed\n");
}

// Test function to validate string output post-MMU
void test_uart_string_late(void) {
    // Static string defined at compile time
    static const char *static_str = "Static string defined at compile time\n";
    
    // Use late UART functions explicitly
    uart_puts_late("[TEST] Testing UART string output (post-MMU)\n");
    uart_puts_late("[TEST] Static string: ");
    uart_puts_late(static_str);
    
    // Try array-based string - safest approach
    volatile char arr_str[] = "Array-based string (stack) - volatile\n";
    uart_puts_late("[TEST] Volatile array string: ");
    uart_puts_late((const char*)arr_str);
    
    // Direct string literal - should work with our robust implementation
    uart_puts_late("[TEST] Direct literal: Hello from direct string literal\n");
    
    // Test with a longer string to stress the system
    uart_puts_late("[TEST] Long string: This is a longer string that would span multiple cache lines and potentially cross page boundaries. The goal is to ensure that our string handling is robust even with longer content.\n");
    
    uart_puts_late("[TEST] Post-MMU UART test completed\n");
}

// Function to run both early and late UART tests
void test_uart_string_all(void) {
    if (mmu_enabled) {
        // Run post-MMU test
        test_uart_string_late();
    } else {
        // Run pre-MMU test
        test_uart_string_early();
    }
} 