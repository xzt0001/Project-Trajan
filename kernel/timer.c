#include "../include/timer.h"
#include "../include/types.h"
#include "../include/uart.h"

// Add include for debug_print
extern void debug_print(const char* msg);

// ARM Generic Timer control registers
#define CNTV_CTL_EL0_ENABLE  (1 << 0)    // Timer Enable
#define CNTV_CTL_EL0_IMASK   (1 << 1)    // Interrupt Mask
#define CNTV_CTL_EL0_ISTATUS (1 << 2)    // Interrupt Status

// GIC registers
#define GICD_BASE           0x08000000
#define GICC_BASE           0x08010000
#define GICD_CTLR          (GICD_BASE + 0x000)  // Distributor Control Register
#define GICD_ISENABLER1    (GICD_BASE + 0x104)  // Interrupt Set-Enable Register 1
#define GICD_ICPENDR1      (GICD_BASE + 0x284)  // Interrupt Clear-Pending Register 1
#define GICD_ISPENDR1      (GICD_BASE + 0x204)  // Interrupt Set-Pending Register 1
#define GICD_IPRIORITYR7   (GICD_BASE + 0x41C)  // Interrupt Priority Register 7
#define GICC_CTLR          (GICC_BASE + 0x000)  // CPU Interface Control Register
#define GICC_PMR           (GICC_BASE + 0x004)  // Priority Mask Register

// Timer interrupt ID
#define TIMER_IRQ_ID       30      // Physical timer IRQ ID
#define TIMER_IRQ_BIT      (1U << (TIMER_IRQ_ID % 32))  // Bit in GICD_ISENABLER1
#define TIMER_INTERVAL     100000  // Timer interval (tuned for QEMU)

// UART constants for debug output
#define UART0_BASE     0x09000000
#define UART0_DR       (UART0_BASE + 0x00)   // Data Register

// Define timer registers for QEMU ARM virt platform
#define TIMER_BASE          0x09000000  // Using UART address temporarily - adjust to real timer
#define TIMER_LOAD          (TIMER_BASE + 0x00)
#define TIMER_VALUE         (TIMER_BASE + 0x04)
#define TIMER_CONTROL       (TIMER_BASE + 0x08)
#define TIMER_INTCLR        (TIMER_BASE + 0x0C)

// Timer control register bits
#define TIMER_ENABLE        (1 << 0)
#define TIMER_PERIODIC      (1 << 1)
#define TIMER_INTEN         (1 << 2)
#define TIMER_32BIT         (1 << 3)

// External function for handling timer ticks
extern void timer_handler(void);

// Static function declarations
static void configure_gic(void);
static void raw_uart_putc(char c);
static void raw_uart_puts(const char *s);

// Simple hardware UART access for debug
static void raw_uart_putc(char c) {
    volatile uint32_t *dr = (volatile uint32_t *)UART0_DR;
    *dr = c;
}

static void raw_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') raw_uart_putc('\r');
        raw_uart_putc(*s++);
    }
}

// Initialize the timer and GIC
void timer_init(void) {
    debug_print("[TIMER] Initializing ARM Generic Timer...\n");
    raw_uart_puts("[TIMER] Initializing timer and GIC\n");
    
    // Step 1: Configure GIC (Generic Interrupt Controller)
    configure_gic();
    
    // Step 2: Make sure we can access timer from EL1
    uint64_t cntkctl_el1;
    asm volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl_el1));
    debug_print("[TIMER] CNTKCTL_EL1 = ");
    uart_print_hex(cntkctl_el1);
    debug_print("\n");
    
    // Set bits to allow EL0 to access virtual timer (bits 1:0) and physical timer (bits 9:8)
    // We're at EL1 but ensure access anyway
    cntkctl_el1 |= (3 << 0) | (3 << 8);
    asm volatile("msr cntkctl_el1, %0" :: "r"(cntkctl_el1));
    
    // Verify settings took effect
    asm volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl_el1));
    debug_print("[TIMER] Updated CNTKCTL_EL1 = ");
    uart_print_hex(cntkctl_el1);
    debug_print("\n");
    
    // Step 3: Configure timer control register
    // Clear control register (disable timer)
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(0));
    debug_print("[TIMER] Timer disabled for configuration\n");
    
    // Set timer interval (how long until next interrupt)
    debug_print("[TIMER] Setting timer interval to: ");
    uart_print_hex(TIMER_INTERVAL);
    debug_print("\n");
    asm volatile("msr cntp_tval_el0, %0" :: "r"(TIMER_INTERVAL));
    
    // Read back timer value to verify
    uint64_t timer_value;
    asm volatile("mrs %0, cntp_tval_el0" : "=r"(timer_value));
    debug_print("[TIMER] Timer value set to: ");
    uart_print_hex(timer_value);
    debug_print("\n");
    
    // Enable timer and unmask interrupt
    debug_print("[TIMER] Enabling timer...\n");
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(CNTV_CTL_EL0_ENABLE));
    
    // Verify timer control setting
    uint64_t timer_control;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(timer_control));
    debug_print("[TIMER] Timer control = ");
    uart_print_hex(timer_control);
    debug_print("\n");
    
    debug_print("[TIMER] Timer initialized successfully\n");
    raw_uart_puts("[TIMER] Initialization complete\n");
}

// Configure GIC for timer interrupts
static void configure_gic(void) {
    raw_uart_puts("[GIC] Configuring GIC...\n");
    debug_print("[GIC] Configuring GIC (Generic Interrupt Controller)\n");
    
    // Step 1: Enable the GIC Distributor
    *((volatile uint32_t*)GICD_CTLR) = 1;
    debug_print("[GIC] GIC Distributor enabled\n");
    
    // Step A: Clear any pending timer interrupt
    *((volatile uint32_t*)GICD_ICPENDR1) = TIMER_IRQ_BIT;
    debug_print("[GIC] Cleared pending timer interrupt\n");
    
    // Step 2: Set timer interrupt priority (lower value = higher priority)
    volatile uint32_t* priority_reg = (volatile uint32_t*)GICD_IPRIORITYR7;
    uint32_t priority_val = *priority_reg;
    debug_print("[GIC] Original priority register = ");
    uart_print_hex(priority_val);
    debug_print("\n");
    
    // Set bits 8-15 to 0xA0 (lower priority value = higher priority)
    priority_val = (priority_val & ~(0xFF << 8)) | (0xA0 << 8);
    *priority_reg = priority_val;
    
    // Verify priority setting
    priority_val = *priority_reg;
    debug_print("[GIC] Updated priority register = ");
    uart_print_hex(priority_val);
    debug_print("\n");
    
    // Step 3: Enable the timer interrupt (IRQ 30)
    volatile uint32_t* enable_reg = (volatile uint32_t*)GICD_ISENABLER1;
    uint32_t enable_val = *enable_reg;
    debug_print("[GIC] Original enable register = ");
    uart_print_hex(enable_val);
    debug_print("\n");
    
    *enable_reg = TIMER_IRQ_BIT;  // Set only bit 30-32 = bit 62
    
    // Verify enable setting
    enable_val = *enable_reg;
    debug_print("[GIC] Updated enable register = ");
    uart_print_hex(enable_val);
    debug_print("\n");
    
    // Step 4: Enable the GIC CPU interface
    *((volatile uint32_t*)GICC_CTLR) = 1;
    debug_print("[GIC] GIC CPU interface enabled\n");
    
    // Step 5: Set the priority mask to allow all interrupts
    *((volatile uint32_t*)GICC_PMR) = 0xFF;
    debug_print("[GIC] Priority mask set to allow all priorities\n");
    
    raw_uart_puts("[GIC] Configuration complete\n");
}

// Function to manually trigger a timer interrupt using GIC
void force_timer_interrupt(void) {
    debug_print("[TIMER] Forcing timer interrupt for testing...\n");
    raw_uart_puts("[TIMER_TEST] Forcing timer interrupt via GIC\n");
    
    // First clear any pending interrupt
    *((volatile uint32_t*)GICD_ICPENDR1) = TIMER_IRQ_BIT;
    
    // Set timer interrupt as pending
    *((volatile uint32_t*)GICD_ISPENDR1) = TIMER_IRQ_BIT;
    
    raw_uart_puts("[TIMER_TEST] Timer interrupt forced - pending bit set\n");
    
    // Wait a bit to see if IRQ fires
    for (volatile int i = 0; i < 1000000; i++) { }
    
    raw_uart_puts("[TIMER_TEST] Timer interrupt test complete\n");
}

// Function to directly test IRQ handler
void test_irq_handler(void) {
    raw_uart_puts("[IRQ_TEST] Directly testing IRQ handler\n");
    
    // Call the IRQ handler directly
    extern void irq_handler(void);
    irq_handler();
    
    raw_uart_puts("[IRQ_TEST] Direct IRQ handler test complete\n");
}

// Initialize timer with the specified interval in milliseconds
void init_timer(int ms_interval) {
    uart_puts("[TIMER] Initializing timer interrupts...\n");
    
    // Call the proper timer initialization function
    timer_init();
    
    // Configure interrupt connection
    init_timer_irq();
    
    // Log timer configuration
    uart_puts("[TIMER] Timer initialized for ");
    uart_puthex(ms_interval);
    uart_puts("ms intervals\n");
    
    uart_puts("[TIMER] Timer setup complete. Waiting for interrupts...\n");
    
    // DO NOT call timer_handler directly here - it will be triggered by interrupts
    // timer_handler();  // This line is causing immediate context switch before IRQs are ready
}

// Function to acknowledge/clear the timer interrupt
void timer_ack(void) {
    // Clear the interrupt - placeholder for real hardware
    // volatile uint32_t* intclr = (volatile uint32_t*)TIMER_INTCLR;
    // *intclr = 1;
}

// Initialize the timer, sets up the interrupt, and configures GIC
void init_timer_irq(void) {
    uart_puts("[TIMER] Setting up timer interrupt connection...\n");
    
    // Configure the GIC to enable timer interrupts
    
    // Step 1: Enable the GIC Distributor
    *((volatile uint32_t*)GICD_CTLR) = 1;
    
    // Step 2: Enable the timer interrupt (IRQ 30)
    *((volatile uint32_t*)GICD_ISENABLER1) = TIMER_IRQ_BIT;
    
    // Step 3: Set timer interrupt priority (lower value = higher priority)
    volatile uint32_t* priority_reg = (volatile uint32_t*)GICD_IPRIORITYR7;
    // Set bits 8-15 to 0xA0 (middle priority)
    uint32_t priority_val = *priority_reg;
    priority_val = (priority_val & ~(0xFF << 8)) | (0xA0 << 8);
    *priority_reg = priority_val;
    
    // Step 4: Enable the GIC CPU interface
    *((volatile uint32_t*)GICC_CTLR) = 1;
    
    // Step 5: Set the priority mask to allow all interrupts
    *((volatile uint32_t*)GICC_PMR) = 0xFF;
    
    uart_puts("[TIMER] Timer interrupt connection established\n");
}
