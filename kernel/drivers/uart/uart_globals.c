#include "../../../include/types.h"
#include "../../../include/uart.h"

// Global variable for UART base address (can be updated at runtime)
// This is used by all UART functions across the codebase
volatile uint32_t* g_uart_base = (volatile uint32_t*)0x09000000; // UART_PHYS

// Define the global MMU status flag
bool mmu_enabled = false; 