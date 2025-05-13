#include "../include/types.h"
#include "../include/uart.h"
#include "../include/vmm.h"

// This file provides compatibility wrappers for legacy code that 
// uses the old UART functions. These will be phased out gradually.

// Function to check if MMU is enabled
// Since we can't directly access the mmu_enabled variable, we'll use this helper
static bool is_mmu_on(void) {
    // Read SCTLR_EL1 register to check if MMU is enabled (bit 0)
    uint64_t sctlr_el1;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_el1));
    return (sctlr_el1 & 1) != 0;
}

// Legacy function implementations that forward to either early or late versions
// based on the MMU state

void uart_init(void) {
    // Call early init by default
    uart_init_early(0);
}

void uart_putc(char c) {
    if (is_mmu_on()) {
        uart_putc_late(c);
    } else {
        uart_putc_early(c);
    }
}

void uart_puts(const char* str) {
    if (!str) return;
    
    if (is_mmu_on()) {
        uart_puts_late(str);
    } else {
        uart_puts_early(str);
    }
}

void uart_hex64(uint64_t value) {
    if (is_mmu_on()) {
        uart_hex64_late(value);
    } else {
        uart_hex64_early(value);
    }
}

void uart_puthex(uint64_t value) {
    if (is_mmu_on()) {
        uart_putc_late('0');
        uart_putc_late('x');
        // Print exactly 8 hex digits with leading zeros for addresses
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            // Convert digit to character and print it
            if (digit < 10) {
                uart_putc_late('0' + digit);
            } else {
                uart_putc_late('a' + (digit - 10));
            }
        }
    } else {
        uart_putc_early('0');
        uart_putc_early('x');
        // Print exactly 8 hex digits with leading zeros for addresses
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            // Convert digit to character and print it
            if (digit < 10) {
                uart_putc_early('0' + digit);
            } else {
                uart_putc_early('a' + (digit - 10));
            }
        }
    }
}

void uart_print_hex(uint64_t value) {
    if (is_mmu_on()) {
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            char c = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
            uart_putc_late(c);
        }
    } else {
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            char c = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
            uart_putc_early(c);
        }
    }
}

void uart_putx(uint64_t value) {
    if (is_mmu_on()) {
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            char c = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
            uart_putc_late(c);
        }
    } else {
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t digit = (value >> i) & 0xF;
            char c = (digit < 10) ? ('0' + digit) : ('A' + (digit - 10));
            uart_putc_early(c);
        }
    }
}

// Legacy raw UART output for very early debugging
void uart_putc_raw(char c) {
    // Always use early version for maximum reliability
    uart_putc_early(c);
}

// Legacy panic function that stops execution
void uart_panic(const char* str) {
    // Output error message using early function for reliability
    uart_puts_early("\n*** PANIC: ");
    if (str) {
        uart_puts_early(str);
    } else {
        uart_puts_early("Unknown error");
    }
    uart_puts_early(" ***\n");
    
    // Halt forever
    while (1) {
        // Empty loop
    }
}

// Set UART base address - used during MMU transition
void uart_set_base(void* addr) {
    // Add stronger memory barriers to ensure all CPUs see the new translation table
    __asm__ volatile("dsb sy" ::: "memory");  // Data Synchronization Barrier (system)
    __asm__ volatile("isb" ::: "memory");     // Instruction Synchronization Barrier
    
    // Global UART base address is updated in the UART module
    g_uart_base = (volatile uint32_t*)addr;
    
    // Call our new function to set the MMU enabled flag
    extern void uart_set_mmu_enabled(void);
    uart_set_mmu_enabled();
    
    // Add more memory barriers after the flag update
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
} 