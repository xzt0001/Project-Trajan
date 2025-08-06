#include "../../../include/task.h"
#include "../../../include/pmm.h"
#include "../../../include/string.h"
#include "../../../include/types.h" // For uint64_t and other types
#include "../../../include/uart.h"  // For uart_puts

// External function declarations
extern void full_restore_context(task_t* task);

// Define page size
#define PAGE_SIZE 4096  // 4KB standard page size

// Add stdarg.h functionality without including stdio.h
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)
// size_t is already defined in types.h

// Forward declare debug_print for use with snprintf
extern void debug_print(const char* msg);

// Simple snprintf implementation (for debugging)
int snprintf(char* buffer, size_t count, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int written = 0;
    size_t i;
    
    // Only handle a limited set of format specifiers for debugging
    for (i = 0; format[i] != '\0' && written < count - 1; i++) {
        if (format[i] != '%') {
            buffer[written++] = format[i];
            continue;
        }
        
        i++; // Skip '%'
        
        switch (format[i]) {
            case 'x': {
                // Handle hex format
                uint64_t val = va_arg(args, uint64_t);
                char hex_chars[] = "0123456789abcdef";
                
                // Skip leading zeros
                int started = 0;
                for (int shift = 60; shift >= 0; shift -= 4) {
                    int digit = (val >> shift) & 0xF;
                    if (digit != 0 || started || shift == 0) {
                        started = 1;
                        buffer[written++] = hex_chars[digit];
                        if (written >= count - 1) break;
                    }
                }
                break;
            }
            case 'l': {
                // Handle %lx format
                if (format[++i] != 'x') {
                    buffer[written++] = '%';
                    buffer[written++] = 'l';
                    buffer[written++] = format[i];
                    break;
                }
                
                uint64_t val = va_arg(args, uint64_t);
                char hex_chars[] = "0123456789abcdef";
                
                // Add 0x prefix
                buffer[written++] = '0';
                if (written >= count - 1) break;
                buffer[written++] = 'x';
                if (written >= count - 1) break;
                
                // Skip leading zeros
                int started = 0;
                for (int shift = 60; shift >= 0; shift -= 4) {
                    int digit = (val >> shift) & 0xF;
                    if (digit != 0 || started || shift == 0) {
                        started = 1;
                        buffer[written++] = hex_chars[digit];
                        if (written >= count - 1) break;
                    }
                }
                break;
            }
            case 'd': {
                // Handle decimal format
                int val = va_arg(args, int);
                
                // Handle negative numbers
                if (val < 0) {
                    buffer[written++] = '-';
                    if (written >= count - 1) break;
                    val = -val;
                }
                
                // Convert to digits
                char digits[20];
                int ndigits = 0;
                
                do {
                    digits[ndigits++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0 && ndigits < 19);
                
                // Output in correct order
                for (int j = ndigits - 1; j >= 0 && written < count - 1; j--) {
                    buffer[written++] = digits[j];
                }
                break;
            }
            case 's': {
                // Handle string format
                char* str = va_arg(args, char*);
                while (*str && written < count - 1) {
                    buffer[written++] = *str++;
                }
                break;
            }
            default:
                // Just output the character
                buffer[written++] = format[i];
                break;
        }
        
        if (written >= count - 1) break;
    }
    
    buffer[written] = '\0';
    va_end(args);
    return written;
}

#define MAX_TASKS 8
task_t* task_list[MAX_TASKS];
int current_task_index = 0;
task_t* current_task = NULL;
int task_count = 0;

// Known good function that's used for testing/verifying
void known_alive_function() {
    volatile uint32_t *uart_raw = (volatile uint32_t *)0x09000000;
    *uart_raw = 'K'; // K for Known function
    *uart_raw = 'F';
    
    // Just loop forever
    while (1) {
        *uart_raw = '.';
        for (volatile int i = 0; i < 1000000; i++);
    }
}

// Import external test pattern function
extern void eret_test_pattern(void) __attribute__((externally_visible));

// Function pointer variable to keep pointers visible to the linker
void (*externally_visible_function_pointer)(void);

// Safe debug print for raw format strings
void debug_print_raw(const char* fmt, ...) {
    // Implementation using direct UART access for maximum reliability
    volatile uint32_t *uart = (volatile uint32_t*)0x09000000;
    const char *p = fmt;
    
    // Get va_list args
    va_list args;
    va_start(args, fmt);
    
    // Print each character until end of string
    while (*p) {
        // Check for format specifier
        if (*p == '%' && *(p+1) != '\0') {
            p++; // Skip '%'
            
            // Handle format specifiers
            if (*p == 'x') {
                // Handle hex format
                uint32_t val = va_arg(args, uint32_t);
                char hex_chars[] = "0123456789abcdef";
                
                // Output 8 hex digits (32 bits)
                for (int shift = 28; shift >= 0; shift -= 4) {
                    uint32_t digit = (val >> shift) & 0xF;
                    *uart = hex_chars[digit];
                }
            } else {
                // Unknown format specifier, just output it
                *uart = '%';
                *uart = *p;
            }
        } else {
            // Regular character
            *uart = *p;
        }
        p++;
    }
    
    va_end(args);
}

// Dummy task that outputs 'A' continuously
void dummy_task_a(void) {
    // For direct testing, just print 'A' a few times and return
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    *uart = 'D'; // D for Dummy task
    *uart = 'A'; // A for task A
    *uart = '\r';
    *uart = '\n';
    
    // Print a few As when called directly from main for testing
    for (int i = 0; i < 3; i++) {
        *uart = 'A';
    }
    
    // When running as a real task, this will loop forever
    if (current_task && current_task->id == 0) {
        while (1) {
            uart_putc('A');
        }
    }
}

// Dummy task that outputs 'B' continuously
void dummy_task_b(void) {
    // Simple task that just outputs 'B' continuously
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    *uart = 'D'; // D for Dummy task
    *uart = 'B'; // B for task B
    *uart = '\r';
    *uart = '\n';
    
    while (1) {
        uart_putc('B');
    }
}

void init_tasks() {
    // Initialize task system variables
    task_count = 0;
    current_task = NULL;
    
    // Clear task list array
    for (int i = 0; i < MAX_TASKS; i++) {
        task_list[i] = NULL;
    }
    
    // Direct UART messages to show progress
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = '[';
    *uart = 'I';
    *uart = 'N';
    *uart = 'I';
    *uart = 'T';
    *uart = '_';
    *uart = 'T';
    *uart = ']';
    *uart = '\r';
    *uart = '\n';
    
    // Create test tasks (for scheduler testing)
    extern void task_a_test(void);
    extern void task_b_test(void);
    extern void task_c_test(void);
    extern void task_d_test(void);
    
    // Check if we're being called from test_scheduler()
    void* caller_address;
    __asm__ volatile("mov %0, x30" : "=r"(caller_address));
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Called from: 0x%lx\n", (uint64_t)caller_address);
    debug_print(buf);
    
    // Create the scheduler test tasks
    *uart = 'C';  // Creating tasks
    *uart = 'T';
    *uart = 'A';
    create_task(task_a_test);
    *uart = 'B';
    create_task(task_b_test);
    *uart = 'C';
    create_task(task_c_test);
    *uart = 'D';
    create_task(task_d_test);
    
    // Set current task
    *uart = 'S';  // Setting current task
    current_task = task_list[0];
    current_task->state = TASK_RUNNING;
    
    // Print out confirmation that tasks are set up
    snprintf(buf, sizeof(buf), "Task A PC: 0x%lx, Entry: 0x%lx\n", 
             current_task->pc, (uint64_t)current_task->entry_point);
    debug_print(buf);
    
    uart_puts("[TASK] Tasks initialized, ready to run\n");
    
    // Don't launch tasks here - main.c will do that for us
}

void create_task(void (*entry_point)()) {
    // Print direct UART debug for task creation
    volatile uint32_t *uart_raw = (volatile uint32_t *)0x09000000;
    *uart_raw = 'T'; // T for Task create
    *uart_raw = 'C';
    *uart_raw = ':';
    
    // Emergency pointer check - print without using snprintf
    if (entry_point == NULL) {
        *uart_raw = '0';  // Null pointer!
        *uart_raw = '!';
        debug_print("FATAL: entry_point is NULL in create_task!\n");
        
        // Don't proceed with null function pointer
        for (;;) {
            *uart_raw = 'n';  // Null function pointer loop
        }
    }
    
    // Directly print the address bytes for critical debugging
    *uart_raw = '@';  // Marker for raw address bytes
    
    // Print entry point address as bytes
    uint8_t* ep_bytes = (uint8_t*)&entry_point;
    for (int i = 7; i >= 0; i--) {
        uint8_t byte = ep_bytes[i];
        uint8_t high = (byte >> 4) & 0xF;
        uint8_t low = byte & 0xF;
        
        *uart_raw = high < 10 ? '0' + high : 'A' + (high - 10);
        *uart_raw = low < 10 ? '0' + low : 'A' + (low - 10);
    }
    
    // Add newline after raw bytes
    *uart_raw = '\r';
    *uart_raw = '\n';
    
    // Print detailed entry point address for debugging
    char buf[32];
    snprintf(buf, sizeof(buf), "EP: 0x%lx\n", (uint64_t)entry_point);
    debug_print(buf);
    
    // Print address of task_a for comparison (to ensure it's imported correctly)
    snprintf(buf, sizeof(buf), "task_a addr: 0x%lx\n", (uint64_t)task_a);
    debug_print(buf);
    
    // Print address of our test pattern
    snprintf(buf, sizeof(buf), "test pattern addr: 0x%lx\n", (uint64_t)eret_test_pattern);
    debug_print(buf);
    
    // Print entry point address first 8 bits in hex to UART directly
    uint64_t ep_high = ((uint64_t)entry_point >> 28) & 0xF;
    if (ep_high < 10) {
        *uart_raw = '0' + ep_high;
    } else {
        *uart_raw = 'A' + (ep_high - 10);
    }
    
    // Check if we've reached maximum number of tasks
    if (task_count >= MAX_TASKS) {
        *uart_raw = 'M'; // M for Max reached
        return;  // Cannot create more tasks
    }
    
    // Allocate memory for task stack (one page = 4KB)
    void* stack = alloc_page();
    if (!stack) {
        *uart_raw = 'S'; // S for Stack alloc failed
        return;  // Failed to allocate memory
    }
    
    // Show stack base address (first character)
    *uart_raw = '0' + ((uint64_t)stack >> 24 & 0xF);
    
    // Initialize the task structure
    task_t* new_task = (task_t*)alloc_page();
    if (!new_task) {
        *uart_raw = 'T'; // T for Task struct alloc failed
        free_page(stack);
        return;  // Failed to allocate memory
    }
    
    // Clear the task structure completely
    memset(new_task, 0, sizeof(task_t));
    
    // Verify entry_point is valid
    if (entry_point == 0) {
        *uart_raw = 'E'; // E for Entry point invalid
        free_page(stack);
        free_page(new_task);
        return;
    }
    
    // Show entry point address (first character)
    *uart_raw = '0' + ((uint64_t)entry_point >> 24 & 0xF);
    
    // Set up the stack pointer (points to the top of the stack)
    // Stack grows downward on ARM64, so point to the end of the allocated page
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack + PAGE_SIZE);
    
    // Ensure 16-byte alignment as required by ARM64 ABI
    stack_top = (uint64_t*)((uint64_t)stack_top & ~0xFUL);
    
    // First, keep the original stack top for reference
    uint64_t* orig_stack_top = stack_top;
    
    // Print raw address of stack memory and top
    char stk_buf[64];
    snprintf(stk_buf, sizeof(stk_buf), "Stack mem: 0x%lx Stack top: 0x%lx\n", 
            (uint64_t)stack, (uint64_t)stack_top);
    debug_print(stk_buf);
    
    // Add logging to confirm stack allocation and alignment
    uart_puts("[DEBUG] task stack VA: ");
    uart_puthex((uint64_t)stack);
    uart_puts("\n");
    
    // Verify stack alignment
    if (((uint64_t)stack_top & 0xF) != 0) {
        debug_print("ERROR: Stack pointer is not 16-byte aligned!\n");
        uart_puts("[ERROR] Task stack not 16-byte aligned!\n");
        *uart_raw = 'A'; // Alignment error
        *uart_raw = '!';
        free_page(stack);
        free_page(new_task);
        return;
    }
    
    // Reserve stack space for register saves (x19-x30, +fp)
    // ARM64 requires 16-byte alignment for the stack
    stack_top -= 16;  // Reserve 128 bytes (16 words) for safe measure
    
    // Re-verify alignment after adjustment
    if (((uint64_t)stack_top & 0xF) != 0) {
        debug_print("ERROR: Adjusted stack pointer is not 16-byte aligned!\n");
        *uart_raw = 'A'; // Alignment error
        *uart_raw = '2';
        free_page(stack);
        free_page(new_task);
        return;
    }
    
    // Print raw address of stack base
    snprintf(stk_buf, sizeof(stk_buf), "Final stack ptr: 0x%lx (aligned: %s)\n", 
            (uint64_t)stack_top, ((uint64_t)stack_top & 0xF) == 0 ? "YES" : "NO");
    debug_print(stk_buf);
    
    // Write a test value to the stack to ensure it's writable
    *stack_top = 0xDEADBEEF;
    
    // Try to read it back to verify memory is accessible
    uint64_t test_value = *stack_top;
    snprintf(stk_buf, sizeof(stk_buf), "Stack test: wrote 0xDEADBEEF, read back 0x%lx\n", test_value);
    debug_print(stk_buf);
    
    // Write test pattern to top 64 bytes of stack
    for (int i = 0; i < 64; i++) {
        ((volatile char*)stack)[i] = 0xAA;
    }
    uart_puts("[DEBUG] Wrote 0xAA pattern to top 64 bytes of stack\n");
    
    // Add a specific pattern to help debug stack corruption
    uint64_t* stack_pattern = (uint64_t*)stack;
    for (int i = 0; i < 8; i++) {
        stack_pattern[i] = 0xDEADBEEF00000000ULL | i;
    }
    uart_puts("[DEBUG] Added 0xDEADBEEF pattern to stack\n");
    
    // Verify memory is mapped and accessible
    if (test_value != 0xDEADBEEF) {
        debug_print("ERROR: Stack memory read verification failed!\n");
        *uart_raw = 'M'; // Memory error
        *uart_raw = '!';
        free_page(stack);
        free_page(new_task);
        return;
    }
    
    // CRITICAL: Ensure memory is cleared to avoid stack corruption
    memset(stack_top, 0, 128);  // Clear register save area
    
    // Now set the stack pointer in the task structure
    new_task->stack_ptr = (uint64_t*)((uint64_t)stack + PAGE_SIZE);  // stack grows downward
    new_task->stack_ptr = (uint64_t*)((uint64_t)new_task->stack_ptr & ~0xF);  // ensure 16-byte alignment
    
    // Double check the stack pointer was set correctly
    if (new_task->stack_ptr != stack_top) {
        debug_print("ERROR: Stack pointer not correctly assigned to task!\n");
        *uart_raw = 'P'; // Pointer error
        *uart_raw = '!';
        free_page(stack);
        free_page(new_task);
        return;
    }
    
    // Print confirmation that stack pointer is set
    snprintf(stk_buf, sizeof(stk_buf), "Task stack_ptr set to: 0x%lx\n", (uint64_t)new_task->stack_ptr);
    debug_print(stk_buf);
    
    // Initialize bottom 16 registers with known pattern
    for (int i = 0; i < 16; i++) {
        // Use 0xAA pattern with register number embedded
        new_task->regs[i] = 0xAA00000000000000UL | i;
    }
    
    // Initialize remaining registers (x16-x30) with different pattern
    for (int i = 16; i < 31; i++) {
        new_task->regs[i] = 0xBB00000000000000UL | i;
    }
    
    // Set up the initial CPU state
    // For first task debugging
    if (task_count == 0) {  // For the first task only
        debug_print("Using provided entry point function\n");
        
        // Immediately verify that the address is non-zero
        if (entry_point == 0 || entry_point == NULL) {
            debug_print("ERROR: entry_point symbol address resolves to NULL!\n");
            *uart_raw = '!';
            *uart_raw = '!';
            *uart_raw = '!';
            // Don't continue with a null function pointer
            while (1) {
                *uart_raw = 'N';  // Signal that address is NULL
            }
        }
        
        // CRITICAL: Advanced diagnostics for the entry point
        volatile uint32_t *debug_uart = (volatile uint32_t *)0x09000000;
        debug_uart[0] = 'D';
        debug_uart[0] = 'I';
        debug_uart[0] = 'A';
        debug_uart[0] = 'G';
        debug_uart[0] = ':';
        debug_uart[0] = ' ';
        
        // Attempt to read the first instruction at the entry point to verify accessibility
        // This will confirm MMU mappings and permissions are correct
        uint32_t *instr_ptr = (uint32_t*)entry_point;
        uint32_t first_instr = 0;
        
        // Read using a volatile pointer to ensure the load is actually performed
        // This is a critical test of memory access before jumping!
        first_instr = *((volatile uint32_t*)entry_point);
        
        // Output first bytes of entry_point
        debug_uart[0] = 'I';
        debug_uart[0] = '=';
        
        // Display the instruction in hex, byte by byte
        for (int i = 0; i < 4; i++) {
            uint8_t byte = (first_instr >> (i * 8)) & 0xFF;
            uint8_t high = (byte >> 4) & 0xF;
            uint8_t low = byte & 0xF;
            
            debug_uart[0] = high < 10 ? '0' + high : 'A' + (high - 10);
            debug_uart[0] = low < 10 ? '0' + low : 'A' + (low - 10);
        }
        
        debug_uart[0] = '\r';
        debug_uart[0] = '\n';
    }
    
    // Compare entry_point with all available test functions
    char cmp_buf[80];
    snprintf(cmp_buf, sizeof(cmp_buf), "task_a=0x%lx test_pattern=0x%lx known=0x%lx\n", 
             (uint64_t)task_a, (uint64_t)eret_test_pattern, (uint64_t)known_alive_function);
    debug_print(cmp_buf);
    
    // For testing purposes, always use the function provided by the caller
    void *actual_entry_point = entry_point;
    
    // Force task_a for the very first task to ensure it's loaded correctly
    if (task_count == 0) {
        externally_visible_function_pointer = task_a;
        debug_print_raw("FORCING TASK_A ENTRY POINT AT: 0x%x\n", (uint32_t)((uint64_t)task_a));
        actual_entry_point = task_a;
    }
    
    // Set PC explicitly to the entry point with proper casting
    new_task->pc = (uint64_t)((uintptr_t)entry_point);
    new_task->spsr = 0x3C5;  // EL1h mode, interrupts off (safe default)
    
    // Add debugging output for task PC
    uart_puts("[DEBUG] Task PC: ");
    uart_puthex((uint64_t)new_task->pc);
    uart_puts("\n");
    
    // Store the entry point in the task structure
    new_task->entry_point = actual_entry_point;
    
    // Print the exact address we're jumping to
    char buf2[64];
    snprintf(buf2, sizeof(buf2), "PC: 0x%lx [%s valid range]\n", 
             (uint64_t)new_task->pc,
             (new_task->pc >= 0 && new_task->pc < 0x200000) ? "INSIDE" : "OUTSIDE");
    debug_print(buf2);
    
    // Print the address as raw bytes for comparison
    snprintf(buf2, sizeof(buf2), "PC bytes: %02x %02x %02x %02x\n", 
             (uint8_t)(new_task->pc >> 24),
             (uint8_t)(new_task->pc >> 16),
             (uint8_t)(new_task->pc >> 8),
             (uint8_t)(new_task->pc));
    debug_print(buf2);
    
    // Verify PC value is valid
    if (new_task->pc == 0 || (new_task->pc & 0x3UL) != 0) {
        *uart_raw = 'P'; // P for PC error
        // PC must be valid and aligned
        *uart_raw = '!';
        // This is a fatal error
        while(1) {
            *uart_raw = 'e'; // Fatal loop
        }
    }
    
    // Verify PC is within executable range
    if (new_task->pc < 0 || new_task->pc >= 0x200000) {
        *uart_raw = 'R'; // R for Range error
        debug_print("ERROR: PC outside executable range [0x0, 0x200000)\n");
        // We'll continue anyway but log it
    }
    
    // Print PC value high bits for debugging
    uint8_t pc_high = ((uint64_t)new_task->pc >> 24) & 0xFF;
    *uart_raw = '0' + ((pc_high >> 4) & 0xF); // High nibble
    *uart_raw = '0' + (pc_high & 0xF);        // Low nibble
    
    // For task state, EL1h mode with interrupts masked for safety
    // 0x3C5 = 0b0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0011_1100_0101
    // - M[3:0] = 0x5 = 0b0101 = EL1h mode (uses SP_EL1)
    // - DAIF[9:6] = 0xC = 0b1100 = Disable IRQ (I=1), Disable FIQ (F=1) for safety
    // - Remaining bits are reserved or N/A
    // This ensures we return to EL1 using SP_EL1 with interrupts disabled initially
    new_task->spsr = 0x3C5;
    
    // Debug output for SPSR value
    uart_puts("[DEBUG] SPSR = ");
    uart_puthex(new_task->spsr);
    uart_puts("\n");
    
    // Double check SPSR is properly set to avoid corruption
    if (new_task->spsr != 0x3C5) {
        debug_print("ERROR: SPSR corruption detected!\n");
        new_task->spsr = 0x3C5;  // Force it again
    }
    
    // Debug - show stack offset from top
    *uart_raw = '0' + (uint64_t)(orig_stack_top - stack_top);
    
    // Initialize other task fields
    new_task->id = task_count;
    new_task->state = TASK_STATE_READY;  // Start as READY, not RUNNING
    
    // Link tasks for round-robin scheduling (create circular list)
    if (task_count > 0) {
        task_list[task_count - 1]->next = new_task;
    }
    new_task->next = task_list[0]; // Circular list for round-robin
    
    // Add to the global task list
    task_list[task_count] = new_task;
    task_count++;
    
    // Task created successfully
    *uart_raw = 'K';  // K for OK
    *uart_raw = '\r';
    *uart_raw = '\n';
    
    // Set current_task if this is the first task (bootstrap scheduler)
    if (task_count == 1) {
        current_task = new_task;
    }
}

// Function to create a task that runs in EL0
void create_el0_task(void (*entry_point)()) {
    // Print direct UART debug for EL0 task creation
    volatile uint32_t *uart_raw = (volatile uint32_t *)0x09000000;
    uart_puts("[TASK] Creating EL0 task with entry point: 0x");
    uart_hex64((uint64_t)entry_point);
    uart_puts("\n");
    
    // Check if we've reached maximum number of tasks
    if (task_count >= MAX_TASKS) {
        uart_puts("[TASK] ERROR: Maximum task count reached\n");
        return;
    }
    
    // Allocate memory for task stack (one page = 4KB)
    void* stack = alloc_page();
    if (!stack) {
        uart_puts("[TASK] ERROR: Failed to allocate stack for EL0 task\n");
        return;
    }
    
    // Initialize the task structure
    task_t* new_task = (task_t*)alloc_page();
    if (!new_task) {
        uart_puts("[TASK] ERROR: Failed to allocate task structure\n");
        free_page(stack);
        return;
    }
    
    // Clear the task structure
    memset(new_task, 0, sizeof(task_t));
    
    // Set up the stack pointer (points to the top of the stack)
    // Stack grows downward on ARM64, so point to the end of the allocated page
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack + PAGE_SIZE);
    
    // Ensure 16-byte alignment as required by ARM64 ABI
    stack_top = (uint64_t*)((uint64_t)stack_top & ~0xFUL);
    
    // Reserve stack space for register saves
    stack_top -= 16;  // Reserve 128 bytes (16 words) for safe measure
    
    // Initialize bottom 16 registers with known pattern
    for (int i = 0; i < 16; i++) {
        new_task->regs[i] = 0xEE00000000000000UL | i;
    }
    
    // Initialize remaining registers (x16-x30) with different pattern
    for (int i = 16; i < 31; i++) {
        new_task->regs[i] = 0xFF00000000000000UL | i;
    }
    
    // Set the stack pointer in the task structure
    new_task->stack_ptr = stack_top;
    
    // Set PC to the entry point
    new_task->pc = (uint64_t)entry_point;
    
    // Set SPSR for EL0t mode with interrupts masked
    // 0x3C0 = 0b0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0011_1100_0000
    // - M[3:0] = 0x0 = 0b0000 = EL0t mode
    // - DAIF[9:6] = 0xC = 0b1100 = Mask IRQ (I=1) and FIQ (F=1)
    new_task->spsr = (1 << 6);  // EL0t mode with IRQ and FIQ masked
    
    // Debug output
    uart_puts("[TASK] Created EL0 task with PC: 0x");
    uart_hex64(new_task->pc);
    uart_puts(", SPSR: 0x");
    uart_hex64(new_task->spsr);
    uart_puts("\n");
    
    // Initialize other task fields
    new_task->id = task_count;
    new_task->state = TASK_STATE_READY;
    snprintf(new_task->name, sizeof(new_task->name), "el0_task_%d", new_task->id);
    new_task->entry_point = entry_point;
    
    // Link tasks for round-robin scheduling
    if (task_count > 0) {
        task_list[task_count - 1]->next = new_task;
    }
    new_task->next = task_list[0];  // Circular list
    
    // Add to the global task list
    task_list[task_count] = new_task;
    task_count++;
    
    // Set current_task if this is the first task
    if (task_count == 1) {
        current_task = new_task;
    }
    
    uart_puts("[TASK] Created EL0 task at 0x");
    uart_hex64((uint64_t)entry_point);
    uart_puts("\n");
}

// Function to directly start a user task in EL0 mode
void start_user_task(void (*entry_point)(void)) {
    uart_puts("[TASK] Starting user task directly at 0x");
    uart_hex64((uint64_t)entry_point);
    uart_puts("\n");
    
    // Allocate memory for task stack (one page = 4KB)
    void* stack = alloc_page();
    if (!stack) {
        uart_puts("[TASK] ERROR: Failed to allocate stack for EL0 task\n");
        return;
    }
    
    // Initialize a task structure for the EL0 task
    task_t* task = (task_t*)alloc_page();
    if (!task) {
        uart_puts("[TASK] ERROR: Failed to allocate task structure\n");
        free_page(stack);
        return;
    }
    
    // Clear the task structure
    memset(task, 0, sizeof(task_t));
    
    // Set up the stack pointer (points to the top of the stack)
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack + PAGE_SIZE);
    
    // Ensure 16-byte alignment
    stack_top = (uint64_t*)((uint64_t)stack_top & ~0xFUL);
    
    // Reserve stack space for register saves
    stack_top -= 16;
    
    // Initialize registers with known patterns for debugging
    for (int i = 0; i < 16; i++) {
        task->regs[i] = 0xEE00000000000000UL | i;
    }
    for (int i = 16; i < 31; i++) {
        task->regs[i] = 0xFF00000000000000UL | i;
    }
    
    // Set the stack pointer in the task structure
    task->stack_ptr = stack_top;
    
    // Set PC to the entry point
    task->pc = (uint64_t)entry_point;
    
    // Set SPSR for EL0t mode
    task->spsr = 0; // EL0t mode (bits 0-3 = 0)
    
    // Debug output
    uart_puts("[TASK] Set up direct EL0 task with PC: 0x");
    uart_hex64(task->pc);
    uart_puts(", SPSR: 0x");
    uart_hex64(task->spsr);
    uart_puts("\n");
    
    // Jump to the EL0 task
    uart_puts("[TASK] Jumping to EL0 task...\n");
    
    // Ensure VBAR_EL1 is set correctly before switching
    uint64_t vbar;
    extern char vector_table[]; // Update type to match global declaration
    asm volatile("mrs %0, vbar_el1" : "=r"(vbar));
    if (vbar != (uint64_t)vector_table) {
        uart_puts("[TASK] WARNING: VBAR_EL1 is not set correctly! Setting it now.\n");
        // Point VBAR_EL1 to the vector table (no &)
        asm volatile("msr vbar_el1, %0" :: "r"(vector_table));
        asm volatile("isb");
    }
    
    // Direct jump to EL0 task
    full_restore_context(task);
    
    // Should never reach here
    uart_puts("[TASK] ERROR: Returned from full_restore_context\n");
    while(1);
}

