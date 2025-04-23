#include "../include/scheduler.h"
#include "../include/task.h"
#include "../include/uart.h"
#include "../include/pmm.h"  // Add include for memory allocation
#include "../include/debug.h"  // Include new debug header

// Add include for debug_print
extern void debug_print(const char* msg);
extern void debug_hex64(const char* label, uint64_t value);

// External declarations for assembly functions
extern void save_context(task_t* task);
extern void restore_context(task_t* task);
extern void full_restore_context(task_t* task);

// Function prototypes
task_t* pick_next_task(void);

// Force visibility of scheduler initialization with special attributes
int scheduler_initialized __attribute__((used, externally_visible, section(".data"))) = 0;

// Helper function to print a task's info
void print_task_info(task_t* task) {
    volatile uint32_t *uart_raw = (volatile uint32_t *)0x09000000;
    
    // Print task ID
    *uart_raw = '#';
    *uart_raw = '0' + task->id;
    *uart_raw = ':';
    
    // Print PC value (first digit in hex)
    uint64_t pc_high = (task->pc >> 28) & 0xF;
    if (pc_high < 10) {
        *uart_raw = '0' + pc_high;
    } else {
        *uart_raw = 'A' + (pc_high - 10);
    }
    
    // Print stack pointer status
    if (task->stack_ptr == NULL) {
        *uart_raw = 'N'; // Null stack pointer
    } else {
        // Check stack alignment
        if (((uint64_t)task->stack_ptr & 0xF) == 0) {
            *uart_raw = 'A'; // Aligned
        } else {
            *uart_raw = 'U'; // Unaligned
        }
    }
    
    // Print state
    *uart_raw = (task->state == TASK_STATE_READY) ? 'R' : 'X';
    
    *uart_raw = ' ';
}

// Original schedule function kept with original implementation
void schedule() {
    // Get next task according to scheduling policy
    task_t* next = pick_next_task();
    if (!next) return;  // No tasks to run
    
    // Update task states
    if (current_task) current_task->state = TASK_READY;
    next->state = TASK_RUNNING;
    current_task = next;
    
    // Perform context switch
    full_restore_context(current_task);
}

// Function to yield CPU to next task
void yield() {
    debug_print("[YIELD] Called\n");
    schedule();
}

// Allocate a page for task stack
uint64_t* task_alloc_page() {
    // Use the existing memory allocation function from pmm.h
    extern void* alloc_page(void);
    void* page = alloc_page();
    
    // Print debug info about allocated page
    dbg_uart("[TASK] Allocated stack @ ");
    debug_hex64("", (uint64_t)page);
    dbg_uart("\n");
    
    return (uint64_t*)page;
}

// Initialize scheduler and tasks
void init_task_scheduler() {
    init_tasks();  // Defined in task.c
}

task_t* pick_next_task(void) {
    if (task_count < 1) return NULL;
    
    // Find current task index
    int current_idx = 0;
    for (int i = 0; i < task_count; i++) {
        if (task_list[i] == current_task) {
            current_idx = i;
            break;
        }
    }
    
    // Simple round-robin for now
    int next_idx = (current_idx + 1) % task_count;
    
    // Return the next runnable task
    return task_list[next_idx];
}

// Task counter variables
volatile int task_a_counter = 0;
volatile int task_b_counter = 0;
volatile int task_c_counter = 0;
volatile int task_d_counter = 0;

void task_a_test() {
    while(1) {
        task_a_counter++;
        uart_putc('A');
        
        // Optional: Add a delay or yield
        for(volatile int i = 0; i < 100000; i++);
        
        // Print counter every 10 iterations
        if (task_a_counter % 10 == 0) {
            uart_puts("\nA:");
            uart_puthex(task_a_counter);
        }
    }
}

void task_b_test() {
    while(1) {
        task_b_counter++;
        uart_putc('B');
        
        // Optional: Add a delay or yield
        for(volatile int i = 0; i < 100000; i++);
        
        // Print counter every 10 iterations
        if (task_b_counter % 10 == 0) {
            uart_puts("\nB:");
            uart_puthex(task_b_counter);
        }
    }
}

void task_c_test() {
    while(1) {
        task_c_counter++;
        uart_putc('C');
        
        // Optional: Add a delay or yield
        for(volatile int i = 0; i < 100000; i++);
        
        // Print counter every 10 iterations
        if (task_c_counter % 10 == 0) {
            uart_puts("\nC:");
            uart_puthex(task_c_counter);
        }
    }
}

void task_d_test() {
    while(1) {
        task_d_counter++;
        uart_putc('D');
        
        // Optional: Add a delay or yield
        for(volatile int i = 0; i < 100000; i++);
        
        // Print counter every 10 iterations
        if (task_d_counter % 10 == 0) {
            uart_puts("\nD:");
            uart_puthex(task_d_counter);
        }
    }
}

void timer_handler() {
    // Display a dot to show timer firing
    uart_putc('.');
    
    // Save current task's context
    if (current_task) {
        save_context(current_task);
    }
    
    // Schedule the next task
    schedule();
    
    // Won't reach here - schedule() doesn't return
}
