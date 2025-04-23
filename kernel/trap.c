// kernel/trap.c
#include "../include/uart.h"
#include "../include/types.h"

// Export symbols for vector table
void sync_el0_handler(void) __attribute__((used, externally_visible));
void irq_el0_handler(void) __attribute__((used, externally_visible));
void fiq_el0_handler(void) __attribute__((used, externally_visible));
void serror_el0_handler(void) __attribute__((used, externally_visible));

void sync_el0_handler(void) {
    uart_puts("[SVC] Trap from EL0 received!\n");
    
    // Read ESR_EL1 to determine the cause of the exception
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    
    // Extract exception class (EC) field
    uint32_t ec = (esr >> 26) & 0x3F;
    
    // Check if it's an SVC instruction (EC = 0x15 for AArch64 SVC)
    if (ec == 0x15) {
        // Extract the immediate value from the SVC instruction
        uint32_t svc_imm = esr & 0xFFFF;
        
        // Print the SVC number
        uart_puts("[SVC] SVC #");
        uart_puthex(svc_imm);
        uart_puts(" called from EL0\n");
        
        // Read the x0 register value that was passed from EL0
        uint64_t x0_value;
        asm volatile("mrs x0, elr_el1\n"  // Get ELR_EL1 (PC where exception occurred)
                     "add x0, x0, #4\n"   // Skip over the SVC instruction (4 bytes)
                     "msr elr_el1, x0\n"  // Update ELR_EL1 to return after SVC
                     "mrs %0, sp_el0" : "=r"(x0_value)); // Get user's x0 from sp_el0
        
        // Display x0 value from user
        uart_puts("[SVC] x0 value from user: ");
        uart_puthex(x0_value);
        uart_puts("\n");
        
        // Loop for now - if we were implementing a real OS we would return to user mode
        for (int i = 10; i > 0; i--) {
            uart_puts("[SVC] Halting in ");
            uart_puthex(i);
            uart_puts(" seconds...\n");
            
            // Simple delay
            for (volatile int j = 0; j < 10000000; j++) { }
        }
    } else {
        // Not an SVC - display the exception class
        uart_puts("[EL0] Synchronous exception with EC=0x");
        uart_puthex(ec);
        uart_puts("\n");
    }
    
    // Halt in an infinite loop
    while (1);
}

void irq_el0_handler(void) {
    uart_puts("[trap] IRQ from EL0\n");
    while (1);
}

void fiq_el0_handler(void) {
    uart_puts("[trap] FIQ from EL0\n");
    while (1);
}

void serror_el0_handler(void) {
    uart_puts("[trap] SERROR from EL0\n");
    while (1);
} 