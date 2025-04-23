#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"  // For access to task_t

// Call this to perform a context switch to the next task
void schedule();

// Assembly-level context switch functions (defined in context.S)
extern void save_context(task_t* task);
extern void restore_context(task_t* task);
extern void full_restore_context(task_t* task);

// Task selection function
task_t* pick_next_task(void);

// New functions for task management
uint64_t* task_alloc_page(void);
void yield(void);
void init_task_scheduler(void);

// Scheduler test functions
void task_a_test(void);
void task_b_test(void);
void task_c_test(void);
void task_d_test(void);
void test_scheduler(void);

// Timer interrupt handler
void timer_handler(void);

#endif
