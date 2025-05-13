// kernel/trap.c
#include "../include/uart.h"
#include "../include/types.h"
#include "../include/syscall.h"  // Include syscall header

// Export symbols for vector table
void sync_el0_handler(void) __attribute__((used, externally_visible));
void irq_el0_handler(void) __attribute__((used, externally_visible));
void fiq_el0_handler(void) __attribute__((used, externally_visible));
void serror_el0_handler(void) __attribute__((used, externally_visible));

// Helper function to print the current execution level
void print_current_el(void) {
    uint64_t el;
    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    el = (el >> 2) & 3;
    uart_puts("[TRAP] Current EL: ");
    uart_puthex(el);
    uart_puts("\n");
}

void sync_el0_handler(void) {
    uart_puts("\n!!! [TRAP] Synchronous trap from EL0 received !!!\n");
    
    // Log CurrentEL to verify if this is really from EL0
    uint64_t currentEL;
    asm volatile("mrs %0, CurrentEL" : "=r"(currentEL));
    currentEL = (currentEL >> 2) & 0x3; // Extract EL bits
    uart_puts("[TRAP] CurrentEL: ");
    uart_puthex(currentEL);
    uart_puts("\n");
    
    print_current_el(); // This is redundant now but kept for backward compatibility
    
    // Read ESR_EL1 to determine the cause of the exception
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    
    // Extract exception class (EC) field
    uint32_t ec = (esr >> 26) & 0x3F;
    
    uart_puts("[TRAP] Exception class (EC): 0x");
    uart_puthex(ec);
    uart_puts("\n");
    
    // Check if it's an SVC instruction (EC = 0x15 for AArch64 SVC)
    if (ec == 0x15) {
        // Extract the immediate value from the SVC instruction
        uint32_t svc_imm = esr & 0xFFFF;
        
        // Print the SVC number
        uart_puts("[TRAP] SVC #");
        uart_puthex(svc_imm);
        uart_puts(" called from EL0\n");
        
        // Read the x0 register value that was passed from EL0
        uint64_t x0_value;
        asm volatile("mrs x0, elr_el1\n"  // Get ELR_EL1 (PC where exception occurred)
                     "add x0, x0, #4\n"   // Skip over the SVC instruction (4 bytes)
                     "msr elr_el1, x0\n"  // Update ELR_EL1 to return after SVC
                     "mrs %0, sp_el0" : "=r"(x0_value)); // Get user's x0 from sp_el0
        
        // Display x0 value from user
        uart_puts("[TRAP] x0 value from user: 0x");
        uart_puthex(x0_value);
        uart_puts("\n");
        
        // Create a basic trap frame with the x0 value
        struct trap_frame tf;
        tf.x0 = x0_value;
        
        uart_puts("[TRAP] Calling syscall_dispatch with number: ");
        uart_puthex(svc_imm);
        uart_puts("\n");
        
        // Call the syscall dispatcher with the syscall number and trap frame
        syscall_dispatch(svc_imm, &tf);
        
        // For now, after handling the syscall, just indicate we're returning to EL0
        uart_puts("[TRAP] Returning to EL0\n");
            
        // Eventually in Step 5, we'll restore user state and return properly
        // But for now, we'll still halt the system
        uart_puts("[TRAP] Halting system after syscall - NOT returning to user mode\n");
    } else {
        // Not an SVC - display the exception class
        uart_puts("[TRAP] Synchronous exception with EC=0x");
        uart_puthex(ec);
        uart_puts("\n");
    }
    
    // Halt in an infinite loop
    uart_puts("[TRAP] Halting in infinite loop\n");
    while (1);
}

// Add handlers for EL1 exceptions to help debugging
void sync_el1_handler(void) __attribute__((used, externally_visible));
void irq_el1_handler(void) __attribute__((used, externally_visible));
void fiq_el1_handler(void) __attribute__((used, externally_visible));
void serror_el1_handler(void) __attribute__((used, externally_visible));

void sync_el1_handler(void) {
    uart_puts("\n!!! [TRAP] Synchronous trap from EL1 received !!!\n");
    print_current_el();
    
    // Read ESR_EL1 to determine the cause of the exception
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    
    // Extract exception class (EC) field
    uint32_t ec = (esr >> 26) & 0x3F;
    
    uart_puts("[TRAP] Exception class (EC): 0x");
    uart_puthex(ec);
    uart_puts("\n");
    
    // Check if it's an SVC instruction (EC = 0x15 for AArch64 SVC)
    if (ec == 0x15) {
        // Extract the immediate value from the SVC instruction
        uint32_t svc_imm = esr & 0xFFFF;
        
        // Print the SVC number
        uart_puts("[TRAP] SVC #");
        uart_puthex(svc_imm);
        uart_puts(" called from EL1\n");
        
        // Read ELR_EL1 for PC where exception occurred
        uint64_t elr;
        asm volatile("mrs %0, elr_el1" : "=r"(elr));
        uart_puts("[TRAP] ELR_EL1 (PC): 0x");
        uart_hex64(elr);
        uart_puts("\n");
        
        // Get x0 value
        uint64_t x0_value;
        asm volatile("mov %0, x0" : "=r"(x0_value));
        
        // Display x0 value
        uart_puts("[TRAP] x0 value: 0x");
        uart_hex64(x0_value);
        uart_puts("\n");
        
        // Create a basic trap frame with the x0 value
        struct trap_frame tf;
        tf.x0 = x0_value;
        
        // Call the syscall dispatcher with the syscall number and trap frame
        syscall_dispatch(svc_imm, &tf);
        
        // Skip over SVC instruction for return
        asm volatile(
            "mrs x0, elr_el1\n"
            "add x0, x0, #4\n"
            "msr elr_el1, x0\n"
            "eret"
        );
    } else {
        // Not an SVC - display the exception class
        uart_puts("[TRAP] Synchronous exception from EL1 with EC=0x");
        uart_puthex(ec);
        uart_puts("\n");
        
        // Halt in an infinite loop
        uart_puts("[TRAP] Halting in infinite loop\n");
        while (1);
    }
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

// Add simple handlers for EL1 traps
void irq_el1_handler(void) {
    uart_puts("[trap] IRQ from EL1\n");
    while (1);
}

void fiq_el1_handler(void) {
    uart_puts("[trap] FIQ from EL1\n");
    while (1);
}

void serror_el1_handler(void) {
    uart_puts("[trap] SERROR from EL1\n");
    while (1);
} 