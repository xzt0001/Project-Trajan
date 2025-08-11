/*
 * exception_tests.c - Exception handling and interrupt testing framework
 * 
 * Provides comprehensive tests for exception delivery, interrupt handling,
 * and system state verification. These tests validate that the exception
 * vector table is properly configured and that handlers work correctly.
 */

#include "../include/selftest.h"
#include "../include/console_api.h"

// Platform constants and hardware register definitions
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

// ARM64 system register bit definitions
#define SCTLR_EL1_M     (1 << 0)   // MMU enable bit
#define DAIF_IRQ_BIT    (1 << 7)   // IRQ bit in DAIF register

// External function declarations
extern char vector_table[];
extern void force_timer_interrupt(void);
extern void enable_irq(void);
extern void test_irq_handler(void);
extern void uart_print_hex(uint64_t value);

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
void test_exception_delivery(void) {
    debug_print("\n===== TESTING EXCEPTION DELIVERY =====\n");
    
    // Print current CPU state for diagnostic purposes
    uint64_t currentEL, daif, vbar, sctlr;
    asm volatile("mrs %0, CurrentEL" : "=r"(currentEL));
    asm volatile("mrs %0, DAIF" : "=r"(daif));
    asm volatile("mrs %0, VBAR_EL1" : "=r"(vbar));
    asm volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
    
    debug_print("Current EL:   0x");
    uart_print_hex(currentEL);
    debug_print("\nDAIF:         0x");
    uart_print_hex(daif);
    debug_print("\nVBAR_EL1:     0x");
    uart_print_hex(vbar);
    debug_print("\nSCTLR_EL1:    0x");
    uart_print_hex(sctlr);
    debug_print("\n");
    
    // Record whether MMU is enabled
    bool mmu_enabled = (sctlr & SCTLR_EL1_M) != 0;
    debug_print("MMU is ");
    debug_print(mmu_enabled ? "ENABLED\n" : "DISABLED\n");
    
    // Test 1: Test SVC exception handling
    debug_print("\nTest 1: Generating SVC instruction...\n");
    asm volatile("svc #0");
    debug_print("Returned from SVC handler\n");
    
    // Test 2: Try to trigger timer interrupt manually
    debug_print("\nTest 2: Manually forcing timer interrupt...\n");
    force_timer_interrupt();
    debug_print("Returned from manual timer interrupt test\n");
    
    // Test 3: Verify interrupt enable/disable state
    uint64_t daif_after;
    asm volatile("mrs %0, DAIF" : "=r"(daif_after));
    bool irqs_enabled = (daif_after & DAIF_IRQ_BIT) == 0;
    debug_print("\nTest 3: Checking IRQ state: IRQs are ");
    debug_print(irqs_enabled ? "ENABLED\n" : "DISABLED\n");
    
    if (!irqs_enabled) {
        debug_print("Enabling interrupts now...\n");
        enable_irq();
        
        // Verify the enable operation worked
        asm volatile("mrs %0, DAIF" : "=r"(daif_after));
        irqs_enabled = (daif_after & DAIF_IRQ_BIT) == 0;
        debug_print("IRQs now: ");
        debug_print(irqs_enabled ? "ENABLED\n" : "DISABLED\n");
    }
    
    // Test 4: Call IRQ handler directly to validate it works
    debug_print("\nTest 4: Directly calling IRQ handler...\n");
    test_irq_handler();
    debug_print("Returned from direct IRQ handler call\n");
    
    // Test 5: Verify VBAR_EL1 mapping by reading vector table
    debug_print("\nTest 5: Reading through vector table mapping...\n");
    uint64_t expected_address = (uint64_t)vector_table;
    debug_print("Expected vector table addr: 0x");
    uart_print_hex(expected_address);
    debug_print("\n");
    
    if (expected_address != vbar) {
        debug_print("WARNING: VBAR_EL1 doesn't match vector table address!\n");
    }
    
    // Try to read first few words of the vector table
    volatile uint32_t* vt_ptr = (volatile uint32_t*)vbar;
    debug_print("Reading vector table at 0x");
    uart_print_hex((uint64_t)vt_ptr);
    debug_print(":\n");
    
    // Safely read first 8 words to verify table accessibility
    for (int i = 0; i < 8; i++) {
        debug_print("  [");
        uart_print_hex(i * 4);
        debug_print("]: 0x");
        
        // Read vector table entry
        volatile uint32_t word = vt_ptr[i];
        
        uart_print_hex(word);
        debug_print("\n");
    }
    
    debug_print("\n===== EXCEPTION TESTING COMPLETE =====\n\n");
}

/**
 * test_exception_handling - Basic exception handling verification
 * 
 * Performs simple SVC exception tests to verify basic exception handling
 * functionality. This is a lighter-weight test suitable for post-MMU
 * environments where full debug infrastructure is available.
 */
void test_exception_handling(void) {
    // Use late UART functions for post-MMU compatibility
    extern void uart_puts_late(const char* str);
    
    uart_puts_late("\n[TEST] Testing exception handling\n");
    
    // Test SVC (Supervisor Call) exception with immediate value 0
    uart_puts_late("[TEST] Triggering SVC #0 exception...\n");
    asm volatile ("svc #0");
    uart_puts_late("[TEST] Successfully returned from SVC #0\n");
    
    // Test SVC with different immediate value
    uart_puts_late("[TEST] Triggering SVC #1 exception...\n");
    asm volatile ("svc #1");
    uart_puts_late("[TEST] Successfully returned from SVC #1\n");
    
    uart_puts_late("[TEST] Exception handling test completed successfully\n");
}

/**
 * test_system_state - Display current system state for debugging
 * 
 * Outputs comprehensive system state information including CPU registers,
 * MMU status, interrupt state, and exception configuration. Useful for
 * debugging exception and system configuration issues.
 */
void test_system_state(void) {
    debug_print("\n===== SYSTEM STATE INSPECTION =====\n");
    
    // Read all relevant system registers
    uint64_t currentEL, daif, vbar, sctlr, ttbr0, ttbr1, tcr, mair;
    
    asm volatile("mrs %0, CurrentEL" : "=r"(currentEL));
    asm volatile("mrs %0, DAIF" : "=r"(daif));
    asm volatile("mrs %0, VBAR_EL1" : "=r"(vbar));
    asm volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
    asm volatile("mrs %0, TTBR0_EL1" : "=r"(ttbr0));
    asm volatile("mrs %0, TTBR1_EL1" : "=r"(ttbr1));
    asm volatile("mrs %0, TCR_EL1" : "=r"(tcr));
    asm volatile("mrs %0, MAIR_EL1" : "=r"(mair));
    
    // Display CPU state
    debug_print("Exception Level: ");
    uart_print_hex((currentEL >> 2) & 0x3);
    debug_print("\nDAIF (Interrupt Mask): 0x");
    uart_print_hex(daif);
    debug_print("\nVBAR_EL1 (Vector Base): 0x");
    uart_print_hex(vbar);
    debug_print("\n");
    
    // Display MMU state
    debug_print("SCTLR_EL1: 0x");
    uart_print_hex(sctlr);
    debug_print("\nMMU Enabled: ");
    debug_print((sctlr & SCTLR_EL1_M) ? "YES\n" : "NO\n");
    
    // Display memory management registers
    debug_print("TTBR0_EL1: 0x");
    uart_print_hex(ttbr0);
    debug_print("\nTTBR1_EL1: 0x");
    uart_print_hex(ttbr1);
    debug_print("\nTCR_EL1: 0x");
    uart_print_hex(tcr);
    debug_print("\nMAIR_EL1: 0x");
    uart_print_hex(mair);
    debug_print("\n");
    
    // Display interrupt state
    bool irqs_enabled = (daif & DAIF_IRQ_BIT) == 0;
    bool fiqs_enabled = (daif & (1 << 6)) == 0;
    bool serrors_enabled = (daif & (1 << 8)) == 0;
    bool debug_enabled = (daif & (1 << 9)) == 0;
    
    debug_print("IRQs: ");
    debug_print(irqs_enabled ? "ENABLED" : "DISABLED");
    debug_print(", FIQs: ");
    debug_print(fiqs_enabled ? "ENABLED" : "DISABLED");
    debug_print("\nSErrors: ");
    debug_print(serrors_enabled ? "ENABLED" : "DISABLED");
    debug_print(", Debug: ");
    debug_print(debug_enabled ? "ENABLED" : "DISABLED");
    debug_print("\n");
    
    debug_print("====================================\n\n");
}

/**
 * test_svc_variants - Test different SVC instruction variants
 * 
 * Tests various SVC immediate values to ensure the SVC handler
 * correctly processes different system call numbers. Useful for
 * validating syscall dispatch logic.
 */
void test_svc_variants(void) {
    debug_print("\n=== Testing SVC Instruction Variants ===\n");
    
    // Test various SVC immediate values
    for (int i = 0; i < 8; i++) {
        debug_print("Testing SVC #");
        uart_print_hex(i);
        debug_print("...\n");
        
        switch (i) {
            case 0: asm volatile("svc #0"); break;
            case 1: asm volatile("svc #1"); break;
            case 2: asm volatile("svc #2"); break;
            case 3: asm volatile("svc #3"); break;
            case 4: asm volatile("svc #4"); break;
            case 5: asm volatile("svc #5"); break;
            case 6: asm volatile("svc #6"); break;
            case 7: asm volatile("svc #7"); break;
        }
        
        debug_print("Returned from SVC #");
        uart_print_hex(i);
        debug_print("\n");
    }
    
    debug_print("SVC variant testing complete\n\n");
}
