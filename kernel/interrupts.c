#include "../include/interrupts.h"
#include "../include/scheduler.h"
#include "../include/uart.h"

// Add include for debug_print
extern void debug_print(const char* msg);

// ARM Generic Timer and GIC registers
#define GICC_BASE       0x08010000
#define GICC_IAR        (GICC_BASE + 0x00C)  // Interrupt Acknowledge Register
#define GICC_EOIR       (GICC_BASE + 0x010)  // End of Interrupt Register

#define TIMER_INTERVAL  100000  // Must match timer.c value
#define TIMER_IRQ_ID    30      // Physical timer IRQ ID

// Hardware UART registers for direct access
#define UART0_BASE     0x09000000
#define UART0_DR       (UART0_BASE + 0x00)   // Data Register
#define UART0_FR       (UART0_BASE + 0x18)   // Flag Register
#define UART0_FR_TXFF  (1 << 5)              // Transmit FIFO Full

// Global counter to track IRQ calls
static volatile int irq_counter = 0;

// Ultra-low level UART output that doesn't rely on any system services
static void raw_uart_putc(char c) {
    // Get pointers to UART registers
    volatile uint32_t *dr = (volatile uint32_t *)UART0_DR;
    volatile uint32_t *fr = (volatile uint32_t *)UART0_FR;
    
    // Wait until UART TX FIFO is not full
    while ((*fr) & UART0_FR_TXFF);
    
    // Write character to the Data Register
    *dr = c;
}

static void raw_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            raw_uart_putc('\r');
        }
        raw_uart_putc(*s++);
    }
}

// IRQ handler - this function is called directly from the vector table
// This is the critical handler for timer interrupts that enables task switching
void irq_handler(void) {
    // Increment IRQ counter
    irq_counter++;
    
    // Try multiple UART output methods to ensure visibility
    
    // 1. Simple uart_putc
    uart_putc('I');
    
    // 2. Direct memory write to UART (simple pointer method)
    volatile uint32_t* uart_raw = (volatile uint32_t*)0x09000000;
    *uart_raw = 'I'; // I for IRQ entry marker
    *uart_raw = 'R'; 
    *uart_raw = 'Q';
    *uart_raw = '#';
    
    // Print counter value
    char digit = '0' + (irq_counter % 10);
    *uart_raw = digit;
    
    *uart_raw = '!';
    *uart_raw = '\r';
    *uart_raw = '\n';
    
    // 3. Ultra-reliable raw UART output with register handling
    raw_uart_puts("\n[IRQ] DIRECT: Handler invoked!\n");
    
    // 4. Read Interrupt Acknowledge Register to get interrupt ID
    uint32_t iar = *((volatile uint32_t*)GICC_IAR);
    uint32_t irq_id = iar & 0x3FF;  // Extract interrupt ID
    
    // Convert IRQ ID to character for raw output
    digit = (irq_id < 10) ? ('0' + irq_id) : ('A' + (irq_id - 10));
    raw_uart_puts("[IRQ] ID: ");
    raw_uart_putc(digit);
    raw_uart_puts("\n");
    
    // 5. Verify this is a timer interrupt (ID 30)
    if (irq_id == TIMER_IRQ_ID) {
        raw_uart_puts("[IRQ] Timer interrupt confirmed\n");
        
        // 6. Call the scheduler to switch tasks
        schedule();
        
        // 7. Reset the timer for next interrupt
        asm volatile("msr cntp_tval_el0, %0" :: "r"(TIMER_INTERVAL));
        
        // 8. Clear the timer interrupt status
        asm volatile("mov x0, #0\n"
                     "msr cntp_ctl_el0, x0\n"      // Disable timer temporarily
                     "mov x0, #1\n"
                     "msr cntp_ctl_el0, x0");      // Re-enable timer
    } else {
        raw_uart_puts("[IRQ] Unknown interrupt\n");
    }
    
    // 9. Write to End of Interrupt Register to acknowledge it
    *((volatile uint32_t*)GICC_EOIR) = iar;
    
    raw_uart_puts("[IRQ] Handler complete\n");
}

// Function to explicitly enable interrupts
void enable_interrupts(void) {
    // Debug output
    debug_print("[INT] Enabling interrupts...\n");
    
    // Direct UART output for maximum visibility
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'I'; *uart = 'N'; *uart = 'T'; *uart = '_'; *uart = 'E'; *uart = 'N'; *uart = '\r'; *uart = '\n';
    
    // Print CPU state before enabling interrupts
    uint64_t daif_before;
    __asm__ volatile("mrs %0, daif" : "=r"(daif_before));
    raw_uart_puts("[INT] DAIF before: 0x");
    for (int i = 7; i >= 0; i--) {
        char hex_digit = ((daif_before >> (i*4)) & 0xF);
        raw_uart_putc(hex_digit < 10 ? '0' + hex_digit : 'A' + (hex_digit - 10));
    }
    raw_uart_puts("\n");
    
    // Clear all DAIF bits to enable interrupts
    __asm__ volatile(
        "msr daifclr, #0xf\n"   // Enable all interrupts (Debug, SError, IRQ, FIQ)
        "isb"                    // Instruction synchronization barrier
    );
    
    // Read back DAIF to verify
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    
    // Print DAIF after enabling interrupts
    raw_uart_puts("[INT] DAIF after: 0x");
    for (int i = 7; i >= 0; i--) {
        char hex_digit = ((daif >> (i*4)) & 0xF);
        raw_uart_putc(hex_digit < 10 ? '0' + hex_digit : 'A' + (hex_digit - 10));
    }
    raw_uart_puts("\n");
    
    // Print status of interrupts
    debug_print("[INT] DAIF status: ");
    if (daif & (1 << 7)) {
        debug_print("IRQ disabled\n");
    } else {
        debug_print("IRQ enabled\n");
    }
    
    if (daif & (1 << 6)) {
        debug_print("[INT] FIQ disabled\n");
    } else {
        debug_print("[INT] FIQ enabled\n");
    }
    
    debug_print("[INT] Interrupts enabled\n");
}

// Enable IRQs by clearing the I bit in DAIF
void enable_irq(void) {
    // Clear only the I bit (bit 7) in DAIF
    __asm__ volatile(
        "msr daifclr, #2\n"  // Clear bit 1 (which is I bit in DAIF)
        "isb"                // Instruction synchronization barrier
    );
    
    // Output message to confirm
    debug_print("[INT] IRQs enabled\n");
}

// Disable IRQs by setting the I bit in DAIF
void disable_irq(void) {
    // Set only the I bit (bit 7) in DAIF
    __asm__ volatile(
        "msr daifset, #2\n"  // Set bit 1 (which is I bit in DAIF)
        "isb"                // Instruction synchronization barrier
    );
    
    // Output message to confirm
    debug_print("[INT] IRQs disabled\n");
}

// Returns true if IRQs are enabled, false otherwise
int irqs_enabled(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    
    // If I bit (bit 7) is set, interrupts are disabled
    return ((daif >> 7) & 1) == 0;
}
