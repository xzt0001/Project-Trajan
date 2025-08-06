#include "../../../include/interrupts.h"
#include "../../../include/scheduler.h"
#include "../../../include/uart.h"
#include "../../../include/timer.h"

// Add debug print declaration
extern void debug_print(const char* msg);

// GIC (Generic Interrupt Controller) registers
#define GICD_BASE       0x08000000  // Distributor
#define GICC_BASE       0x08010000  // CPU Interface

// GIC CPU Interface registers
#define GICC_IAR        (GICC_BASE + 0x00C)  // Interrupt Acknowledge Register
#define GICC_EOIR       (GICC_BASE + 0x010)  // End of Interrupt Register

// Timer configuration
#define TIMER_IRQ       30          // Physical timer IRQ ID
#define TIMER_INTERVAL  100000      // Default timer interval

void handle_irq() {
    // VERY FIRST OUTPUT - Raw UART output to confirm entry
    volatile uint32_t *uart_raw = (volatile uint32_t *)0x09000000;
    *uart_raw = 'I';  // 'I' for IRQ handler
    *uart_raw = 'R';
    *uart_raw = 'Q';
    *uart_raw = '!';
    *uart_raw = '\r';
    *uart_raw = '\n';
    
    // Vector debug check
    debug_print("[VECTOR] IRQ vector active\n");
    
    // Regular debug output
    debug_print("************************\n");
    debug_print("[IRQ] Interrupt received!\n");
    debug_print("************************\n");
    uart_puts("[IRQ] Interrupt received!\n");

    // Read interrupt ID from GIC
    uint32_t iar = *((volatile uint32_t*)GICC_IAR);
    uint32_t id = iar & 0x3FF; // Extract interrupt ID (bits 0-9)

    debug_print("[IRQ] Interrupt ID: ");
    if (id == TIMER_IRQ) {
        debug_print("30 (Timer)\n");
        uart_puts("[IRQ] Timer interrupt!\n");
        
        // 1. Reset the timer to clear the pending state
        asm volatile("msr cntp_tval_el0, %0" :: "r"(TIMER_INTERVAL));
        
        // 2. Explicitly clear the timer interrupt by toggling control bits
        uint64_t ctrl;
        asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctrl));
        asm volatile("msr cntp_ctl_el0, xzr");  // Disable timer temporarily
        asm volatile("msr cntp_ctl_el0, %0" :: "r"(ctrl));  // Re-enable with same settings
        
        debug_print("[IRQ] Timer reset for next interrupt\n");
        
        // 3. Call scheduler to switch tasks
        debug_print("[IRQ] About to call scheduler...\n");
        schedule();
        debug_print("[IRQ] Returned from scheduler!\n");
    } else if (id == 0) {
        debug_print("Spurious interrupt\n");
    } else {
        debug_print("Unknown (");
        // Print the IRQ number
        char digit = '0' + (id % 10);
        if (id >= 10) {
            char tens = '0' + (id / 10);
            debug_print(&tens);
        }
        debug_print(&digit);
        debug_print(")\n");
        uart_puts("[IRQ] Unknown interrupt!\n");
    }

    // 4. Acknowledge the interrupt at the GIC level
    *((volatile uint32_t*)GICC_EOIR) = iar;
    debug_print("[IRQ] Interrupt acknowledged at GIC\n");
    
    debug_print("[IRQ] Handler complete\n");
} 