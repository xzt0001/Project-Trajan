#ifndef INTERRUPTS_H
#define INTERRUPTS_H

// Initialize the GIC (Generic Interrupt Controller)
void init_gic(void);

// Configure and enable the timer interrupt
void setup_timer_irq(void);

// Enable IRQs by clearing the I bit in DAIF
void enable_irq(void);

// Disable IRQs by setting the I bit in DAIF
void disable_irq(void);

// Returns true if IRQs are enabled, false otherwise
int irqs_enabled(void);

#endif
