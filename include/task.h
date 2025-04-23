#ifndef TASK_H
#define TASK_H

#include "../include/types.h"

#define TASK_STATE_READY   0
#define TASK_STATE_RUNNING 1
#define MAX_TASKS          8

// Task state enum for better readability
typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED
} task_state_t;

typedef struct task {
    uint64_t* stack_ptr;       // Stack pointer
    uint64_t regs[31];         // General-purpose registers
    uint64_t pc;               // Program counter
    uint64_t spsr;             // Saved program status

    int id;
    int state;
    
    char name[16];             // Task name
    void (*entry_point)(void); // Function pointer for task entry point

    struct task* next;
} task_t;

extern task_t* current_task;
extern int task_count;
extern task_t* task_list[MAX_TASKS];

// Task management functions
void init_tasks();
void create_task(void (*entry_point)());
void create_el0_task(void (*entry_point)()); // Create a task that runs in EL0 mode
void dummy_task_a(void);  // Dummy task function
void dummy_task_b(void);  // Dummy task function

// Declare task functions with noreturn attribute
void task_a(void) __attribute__((noreturn));
void task_b(void) __attribute__((noreturn));

// Utility functions used in task initialization
extern void debug_print(const char* msg);

#endif
