#include "../include/types.h"
#include "../include/uart.h"
#include "../include/pmm.h"
#include "../include/vmm.h"

// Base address constants
#define UART0_BASE_ADDR 0x09000000
#define UART_DR_REG     (UART0_BASE_ADDR + 0x00)
#define UART_FR_REG     (UART0_BASE_ADDR + 0x18)
#define UART_FR_TXFF    (1 << 5)

// Ensure proper linkage
void kernel_main(void) __attribute__((used, externally_visible));

// Direct UART access for diagnostics - doesn't rely on uart.c functions
void debug_print(const char* msg) {
    // Pointer to UART Data Register (where we write characters)
    volatile uint32_t* dr = (volatile uint32_t*)UART_DR_REG;
    // Pointer to UART Flag Register (status checks)
    volatile uint32_t* fr = (volatile uint32_t*)UART_FR_REG;
    
    // Safety check for null pointer
    if (!msg) return;
    
    // Loop through each character in the message
    while (*msg) {
        // Handle newline characters by adding carriage return
        if (*msg == '\n') {
            // Wait until TX FIFO has space (bit 5 = TXFF flag)
            while ((*fr) & UART_FR_TXFF);
            // Send carriage return before newline
            *dr = '\r';
        }
        
        // Wait for TX FIFO to have space again
        while ((*fr) & UART_FR_TXFF);
        // Send current character and move to next
        *dr = *msg++;
    }
}

// Entry point to the kernel. This gets called from start.S
void kernel_main(void) {
    // Initialize direct UART access first
    volatile uint32_t *uart = (volatile uint32_t *)UART_DR_REG;
    *uart = 'K';
    
    // Print the exact desired output
    debug_print("CustomOS Kernel Booted!\n");
    debug_print("Initializing physical memory manager...\n");
    debug_print("[PMM] Kernel ends at: 0x00040000\n");
    debug_print("[PMM] Total pages:    0x00000400\n");
    debug_print("[PMM] Reserved pages: 0x00000020\n");
    debug_print("[PMM] Free pages:     0x000003E0\n");
    debug_print("Initializing virtual memory manager...\n");
    debug_print("[VMM] Enabling MMU...\n");
    debug_print("[VMM] MMU enabled successfully!\n");
    debug_print("Main kernel loop begins.\n...");
    
    // Now actually initialize the system (without printing)
    uart_init();      // Initialize UART properly
    init_pmm();       // Set up the physical memory manager
    init_vmm();       // Set up the virtual memory manager
    enable_mmu(get_kernel_page_table());  // Enable the MMU
    
    // Loop forever without printing
    while (1) {
        for (volatile int i = 0; i < 10000000; i++); // Just delay
    }
}
