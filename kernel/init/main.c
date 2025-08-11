#include "../../include/uart.h"
#include "../../include/task.h"
#include "../../include/pmm.h"
#include "../../include/vmm.h"
#include "../../include/address_space.h"
#include "../../include/scheduler.h"
#include "../../include/timer.h"
#include "../../include/types.h"
#include "../../include/interrupts.h"
#include "../../include/string.h"  // Add string.h for memset
#include "include/panic.h"        // New modular panic API
#include "include/console_api.h"  // New modular console API
#include "include/memory_debug.h" // New modular memory debug API
#include "include/arch_ops.h"     // New modular architecture operations API
#include "include/sample_tasks.h" // New modular sample tasks API
#include "include/selftest.h"     // New modular self-test framework API

// Add the test_context_switch declaration
extern void test_context_switch(void);

// Add declaration for snprintf function
extern int snprintf(char* buffer, size_t count, const char* format, ...);



// Ensure kernel_main is properly exported - forward declaration with all attributes
__attribute__((used, externally_visible, noinline, section(".text.boot.main")))
void kernel_main(unsigned long uart_addr);

// Replace <stdbool.h> with our own boolean type definitions for freestanding environment
// REMOVED duplicate bool type definition - already in types.h

// Base address constants
#define UART0_BASE_ADDR 0x09000000
#define UART_DR_REG     (UART0_BASE_ADDR + 0x00)
#define UART_FR_REG     (UART0_BASE_ADDR + 0x18)
#define UART_FR_TXFF    (1 << 5)

// Vector table symbols defined in linker script
extern char vector_table[];            // Virtual address (0x1000000)
extern char _vector_table_load_start[]; // Physical load address (0x89000)



// Forward declarations
extern void full_restore_context(task_t* task);
extern uint64_t* get_kernel_page_table(void);  // Level 0 table
extern void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
extern void ensure_vector_table_executable_l3(uint64_t* l3_table);  // Use L3 table version
extern uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);

// Function declarations for init functions
extern void init_pmm(void);
extern void init_vmm(void);
extern void init_vmm_wrapper(void); // Add wrapper function declaration
extern void create_task(void (*entry_point)()); // Corrected function declaration

// Flags for mapping memory (copied from vmm.c)
#define PTE_VALID       (1UL << 0)  // Entry is valid
#define PTE_TABLE       (1UL << 1)  // Entry points to another table (vs block/page)
#define PTE_AF          (1UL << 10) // Access flag - set when page is accessed
// REMOVED conflicting PTE_SH_INNER definition - use the one from vmm.h
#define PTE_AP_RW       (0UL << 6)  // Access permissions: Read/Write at any EL
#define ATTR_NORMAL_IDX 0           // Index for normal memory attributes
#define ATTR_NORMAL     (ATTR_NORMAL_IDX << 2)  // Normal memory attribute
#define PTE_PXN         (1UL << 53) // Privileged Execute Never (kernel can't execute)
#define PTE_UXN         (1UL << 54) // Unprivileged Execute Never (user can't execute)
#define PAGE_SIZE       4096        // Standard 4KB page size

// Forward declarations for functions used
void init_exceptions(void);
void set_vbar_el1(unsigned long addr);

// Define DEBUG_UART if not defined elsewhere
#ifndef DEBUG_UART
#define DEBUG_UART 0x09000000
#endif

















// Declare the assembly function
extern void test_uart_directly(void);









// Include these specific register definitions
#define VECTOR_ADDR_ALIGNMENT 0x800 // 2KB alignment
#define SCTLR_EL1_M (1 << 0)  // MMU enable bit
#define DAIF_IRQ_BIT (1 << 7) // IRQ bit in DAIF register

// External references to vector table
extern void set_vbar_el1(unsigned long addr);



// Test function to generate exceptions for testing purposes












// Add declaration for the EL0 task creation function and user_task
extern void create_el0_task(void (*entry_point)()); 
extern void user_test_svc(void);  // Changed from user_task to user_test_svc

// Vector table copy symbols defined in linker script
extern char _vector_table_source_start[];
extern char _vector_table_source_end[];
extern char _vector_table_dest_start[];















// Entry point to the kernel. This gets called from start.S
__attribute__((used, externally_visible, noinline, section(".text.boot.main")))
void kernel_main(unsigned long uart_addr) {
    volatile unsigned int* uart = (unsigned int*)0x09000000;
    
    // Clear the UART output for a fresh start
    uart_clear_screen();
    
    // Output early stage marker 'A' directly to UART
    uart_debug_marker('A'); // Start of kernel_main
    
    // Initialize the UART for serial output - use early version explicitly
    uart_init_early(uart_addr);
    
    // Banner and version - explicitly use early functions before MMU
    uart_puts_early("\n\n===========[ CustomOS Kernel ]============\n");
    uart_puts_early("Version 0.1.0 - Boot Sequence\n");
    uart_puts_early("========================================\n\n");
    
    // Initialize memory management using unified interface
    *uart = 'M';  // Memory initialization marker
    extern int init_memory_subsystem(void);
    int memory_result = init_memory_subsystem();
    if (memory_result == 0) {
        *uart = '0';  // Full MMU success marker
        *uart = 'F'; *uart = 'U'; *uart = 'L'; // FUL - Full MMU support
    } else if (memory_result == 1) {
        *uart = '1';  // PMM-only mode marker
        *uart = 'B'; *uart = 'Y'; *uart = 'P'; // BYP - Bypass mode
    } else {
        *uart = '!';  // Error marker
        *uart = 'E'; *uart = 'R'; *uart = 'R'; // ERR - Error
    }
    uart_debug_marker('B'); // After memory initialization
    
    // Validate vector table at physical address before MMU
    *uart = 'V'; *uart = 'T'; *uart = 'C'; // VTC - Vector Table Check
    validate_vector_table_at_0x89000();
    
    // Improved vector table verification with clear markers
    *uart = 'V'; *uart = 'T'; *uart = 'V'; // VTV - Vector Table Verification
    volatile uint8_t* p = (volatile uint8_t*)0x00089000;
    
    // Output first 32 bytes in a clean format
    for (int i = 0; i < 32; i++) {
        if (i % 8 == 0) {
            *uart = '\n';
            *uart = '0'; *uart = 'x'; // 0x prefix
            // Output address
            uint64_t addr = (uint64_t)(p + i);
            for (int j = 7; j >= 0; j--) {
                uint8_t byte = (addr >> (j * 8)) & 0xFF;
                uint8_t hi = (byte >> 4) & 0xF;
                uint8_t lo = byte & 0xF;
                *uart = hi < 10 ? '0' + hi : 'A' + (hi - 10);
                *uart = lo < 10 ? '0' + lo : 'A' + (lo - 10);
            }
            *uart = ':'; *uart = ' ';
        }
        
        char hi = (p[i] >> 4) & 0xF;
        char lo = p[i] & 0xF;
        *uart = hi < 10 ? '0' + hi : 'A' + (hi - 10);
        *uart = lo < 10 ? '0' + lo : 'A' + (lo - 10);
        *uart = ' ';
    }
    *uart = '\n';
    
    uart_debug_marker('C'); // After vector table verification
    
    // Perform cache maintenance for vector table
    *uart = 'C'; *uart = 'M'; *uart = 'V'; // CMV - Cache Maintenance Vector
    asm volatile("dc cvau, %0" :: "r" (0x89000) : "memory");
    asm volatile("dsb ish");
    asm volatile("isb");
    
    // Set VBAR_EL1 to physical address BEFORE MMU
    *uart = 'V'; *uart = 'B'; *uart = 'S'; // VBS - VBAR Set
    write_vbar_el1(0x89000);
    uart_debug_marker('D'); // After setting VBAR_EL1 to 0x89000
    
    // Check if MMU was successfully initialized
    if (memory_result == 0) {
        // Full MMU mode - MMU is already enabled by init_memory_subsystem
        *uart = 'M'; *uart = 'M'; *uart = 'U'; // MMU - MMU enabled
        uart_puts_late("[BOOT] MMU is enabled, virtual addressing is active\n");
    } else {
        // Bypass mode - continue with physical addressing
        *uart = 'P'; *uart = 'H'; *uart = 'Y'; // PHY - Physical addressing
    }
    
    // Handle post-initialization setup based on memory mode
    if (memory_result == 0) {
        // After MMU is enabled, update VBAR_EL1 to virtual address
        // CRITICAL: Post-MMU code must use uart_puts_late
        uart_puts_late("[BOOT] Updating VBAR_EL1 to virtual 0x1000000 after MMU\n");
        write_vbar_el1(0x1000000);
        uart_debug_marker_late('F'); // After setting VBAR_EL1 to 0x1000000
        
        // Test UART string output after MMU enable
        // Use string literals directly inside this function to avoid stale pointer issues
        uart_puts_late("[BOOT] Testing UART string output after MMU is enabled\n");
        test_uart_after_mmu();
        uart_debug_marker_late('G'); // After UART MMU test
    } else {
        // Bypass mode - continue with physical addressing
        uart_puts_early("[BOOT] Continuing with physical UART at 0x89000\n");
        uart_debug_marker('F'); // After bypass mode setup
    }
    
    // Run exception handling tests
    test_exception_handling();
    
    // Continue with initialization using appropriate UART function
    if (memory_result == 0) {
        uart_puts_late("\n[BOOT] Continuing kernel initialization...\n");
    } else {
        uart_puts_early("\n[BOOT] Continuing kernel initialization...\n");
    }
}
