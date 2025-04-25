#include "../include/uart.h"

// User task entry point for EL0 execution
void user_task_entry(void) {
    uart_puts(">>> EL0 USER TASK STARTED <<<\n");
    
    asm volatile("mov x0, #0\n svc #0");         // sys_hello
    asm volatile("mov x0, #0x1234\n svc #1");    // sys_write
    asm volatile("svc #3");                      // sys_yield
    asm volatile("mov x0, #42\n svc #2");        // sys_exit
    
    while (1); // fallback loop
} 