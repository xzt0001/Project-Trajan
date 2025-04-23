#ifndef TIMER_H
#define TIMER_H

#include "interrupts.h" // Include interrupts.h for IRQ control functions

// Initialize timer with the specified interval in milliseconds
void init_timer(int ms_interval);

// Function to acknowledge/clear the timer interrupt
void timer_ack(void);

// Function to manually force a timer interrupt (for testing)
void force_timer_interrupt(void);

// Initialize the timer, sets up the interrupt, and configures GIC
void init_timer_irq(void);

// Directly test the IRQ handler function
void test_irq_handler(void);

#endif
