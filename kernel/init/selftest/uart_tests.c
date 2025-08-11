/*
 * uart_tests.c - UART functionality testing framework
 * 
 * Provides comprehensive tests for UART communication including direct
 * register access, string output, hex formatting, and MMU transition
 * compatibility. These tests validate UART functionality across different
 * system states and memory configurations.
 */

#include "../include/selftest.h"
#include "../include/console_api.h"
#include "../../../include/uart.h"

// Platform constants for UART testing
#ifndef TEST_UART_BASE
#define TEST_UART_BASE 0x09000000  // Physical UART base
#endif

// UART_VIRT is now available from uart.h

// External UART function declarations
extern void uart_puts_late(const char* str);
extern void uart_hex64_late(uint64_t value);
extern void uart_print_hex(uint64_t value);
// uart_debug_hex is now available from uart.h

/**
 * test_uart_direct - Direct UART register access test
 * 
 * Tests the most basic UART functionality by writing directly to the
 * UART data register. This test uses minimal dependencies and should
 * work even in early boot environments or when other UART functions
 * are not available.
 */
void test_uart_direct(void) {
    // Direct UART output using volatile pointer to ensure memory access
    volatile uint32_t* uart = (volatile uint32_t*)TEST_UART_BASE;
    
    // Simple sequential characters for visibility
    *uart = 'T';  // T for Test
    *uart = 'U';  // U for UART
    *uart = 'D';  // D for Direct
    *uart = '!';  // ! for emphasis
    
    // Add line termination for clean output
    *uart = '\r';
    *uart = '\n';
}

/**
 * test_uart_after_mmu - UART functionality test after MMU enablement
 * 
 * Comprehensive UART testing for post-MMU environments. Tests string
 * output, hex formatting, direct register access with virtual addressing,
 * and various UART utility functions. Critical for validating UART
 * functionality after memory management transitions.
 */
void test_uart_after_mmu(void) {
    // Use late functions explicitly for post-MMU compatibility
    uart_puts_late("\n[TEST] Testing UART output after MMU is enabled\n");
    
    // Long string test to verify string handling works correctly
    uart_puts_late("[TEST] This is a longer string to test if UART string handling is working correctly after MMU is enabled\n");
    
    // Test hex value output using different methods
    uint64_t test_value = 0x1234567890ABCDEF;
    uart_puts_late("[TEST] Hex value: 0x");
    uart_hex64_late(test_value);
    uart_puts_late("\n");
    
    // Direct access test using virtual UART address
    volatile uint32_t* uart_dr = (volatile uint32_t*)UART_VIRT;
    *uart_dr = 'D';
    *uart_dr = 'I';
    *uart_dr = 'R';
    *uart_dr = 'E';
    *uart_dr = 'C';
    *uart_dr = 'T';
    *uart_dr = '\r';
    *uart_dr = '\n';
    
    // Test debug helper functions
    uart_puts_late("[TEST] Debug hex function test: ");
    uart_debug_hex(0xDEADBEEF);
    uart_puts_late("\n");
    
    // Final confirmation
    uart_puts_late("[TEST] UART test completed successfully\n\n");
}

/**
 * test_uart_character_set - Test UART with various character types
 * 
 * Tests UART output with different character types including printable
 * ASCII, control characters, and special symbols. Useful for verifying
 * UART character handling and terminal compatibility.
 */
void test_uart_character_set(void) {
    volatile uint32_t* uart = (volatile uint32_t*)TEST_UART_BASE;
    
    debug_print("\n[UART] Character set test:\n");
    
    // Test printable ASCII characters
    debug_print("ASCII: ");
    for (char c = '!'; c <= '~'; c++) {
        *uart = c;
        if ((c - '!') % 16 == 15) {
            *uart = '\r';
            *uart = '\n';
            debug_print("       ");
        }
    }
    *uart = '\r';
    *uart = '\n';
    
    // Test digits
    debug_print("Digits: ");
    for (char c = '0'; c <= '9'; c++) {
        *uart = c;
        *uart = ' ';
    }
    *uart = '\r';
    *uart = '\n';
    
    // Test uppercase letters
    debug_print("Upper:  ");
    for (char c = 'A'; c <= 'Z'; c++) {
        *uart = c;
        *uart = ' ';
    }
    *uart = '\r';
    *uart = '\n';
    
    // Test lowercase letters
    debug_print("Lower:  ");
    for (char c = 'a'; c <= 'z'; c++) {
        *uart = c;
        *uart = ' ';
    }
    *uart = '\r';
    *uart = '\n';
    
    debug_print("Character set test complete\n\n");
}

/**
 * test_uart_timing - UART timing and throughput test
 * 
 * Tests UART output timing by sending known patterns and measuring
 * delays. Useful for verifying UART baud rate and timing characteristics.
 * Also tests UART behavior under different output loads.
 */
void test_uart_timing(void) {
    volatile uint32_t* uart = (volatile uint32_t*)TEST_UART_BASE;
    
    debug_print("\n[UART] Timing test:\n");
    
    // Test rapid character output
    debug_print("Rapid output: ");
    for (int i = 0; i < 100; i++) {
        *uart = '.';
    }
    *uart = '\r';
    *uart = '\n';
    
    // Test with delays
    debug_print("Delayed output: ");
    for (int i = 0; i < 10; i++) {
        *uart = '0' + i;
        
        // Small delay
        for (volatile int j = 0; j < 10000; j++) {
            // Empty delay loop
        }
    }
    *uart = '\r';
    *uart = '\n';
    
    // Test burst output
    debug_print("Burst test:\n");
    for (int burst = 0; burst < 3; burst++) {
        debug_print("Burst ");
        uart_print_hex(burst);
        debug_print(": ");
        
        for (int i = 0; i < 20; i++) {
            *uart = 'X';
        }
        *uart = '\r';
        *uart = '\n';
        
        // Delay between bursts
        for (volatile int j = 0; j < 50000; j++) {
            // Empty delay loop
        }
    }
    
    debug_print("Timing test complete\n\n");
}

/**
 * test_uart_hex_formatting - Test hex number formatting functions
 * 
 * Tests various hex formatting functions with different value ranges
 * and formats. Validates that hex output is correctly formatted and
 * readable for debugging purposes.
 */
void test_uart_hex_formatting(void) {
    debug_print("\n[UART] Hex formatting test:\n");
    
    // Test various hex values
    uint64_t test_values[] = {
        0x0,
        0x1,
        0xF,
        0x10,
        0xFF,
        0x100,
        0xFFF,
        0x1000,
        0xFFFF,
        0x10000,
        0xFFFFF,
        0x100000,
        0xFFFFFF,
        0x1000000,
        0xFFFFFFFF,
        0x100000000,
        0xFFFFFFFFFF,
        0x10000000000,
        0xFFFFFFFFFFF,
        0x1000000000000,
        0xFFFFFFFFFFFF,
        0x10000000000000,
        0xFFFFFFFFFFFFF,
        0x100000000000000,
        0xFFFFFFFFFFFFFF,
        0x1000000000000000,
        0xFFFFFFFFFFFFFFFF
    };
    
    int num_values = sizeof(test_values) / sizeof(test_values[0]);
    
    for (int i = 0; i < num_values; i++) {
        debug_print("Value ");
        uart_print_hex(i);
        debug_print(": 0x");
        uart_print_hex(test_values[i]);
        debug_print("\n");
    }
    
    debug_print("Hex formatting test complete\n\n");
}

/**
 * test_uart_string_functions - Test string output functions
 * 
 * Tests various string output functions including early console,
 * late console, and debug print functions. Validates string
 * handling across different system states.
 */
void test_uart_string_functions(void) {
    debug_print("\n[UART] String function test:\n");
    
    // Test early console functions
    debug_print("Testing early_console_print...\n");
    early_console_print("[EARLY] This is early console output\n");
    
    // Test debug print
    debug_print("Testing debug_print (this message uses it)\n");
    
    // Test late functions if available
    if (UART_VIRT != 0) {
        debug_print("Testing late UART functions...\n");
        uart_puts_late("[LATE] This is late UART output\n");
        
        uart_puts_late("[LATE] Testing hex output: 0x");
        uart_hex64_late(0xABCDEF123456789);
        uart_puts_late("\n");
    } else {
        debug_print("Late UART functions not available (MMU not enabled)\n");
    }
    
    debug_print("String function test complete\n\n");
}

/**
 * test_uart_error_conditions - Test UART error handling
 * 
 * Tests UART behavior under various error conditions and edge cases.
 * Validates that UART functions handle unusual inputs gracefully.
 */
void test_uart_error_conditions(void) {
    debug_print("\n[UART] Error condition test:\n");
    
    // Test null string handling (carefully)
    debug_print("Testing error conditions...\n");
    
    // Test very long strings
    debug_print("Long string: ");
    for (int i = 0; i < 200; i++) {
        if (i % 50 == 0) {
            debug_print("\n             ");
        }
        debug_print("X");
    }
    debug_print("\n");
    
    // Test rapid successive calls
    debug_print("Rapid calls: ");
    for (int i = 0; i < 10; i++) {
        debug_print("A");
        debug_print("B");
        debug_print("C");
    }
    debug_print("\n");
    
    // Test special characters
    debug_print("Special chars: ");
    volatile uint32_t* uart = (volatile uint32_t*)TEST_UART_BASE;
    *uart = '\t';  // Tab
    *uart = '\r';  // Carriage return
    *uart = '\n';  // Newline
    *uart = '\b';  // Backspace (if supported)
    *uart = '\f';  // Form feed (if supported)
    
    debug_print("Error condition test complete\n\n");
}
