#pragma once
#include "types.h"

// Syscall numbers
#define SYS_HELLO   0
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_YIELD   3

// Called by trap handler
// Basic trap frame structure - will be expanded in Step 4
struct trap_frame {
    // General purpose registers
    uint64_t x0;
    // Other registers will be added in Step 4
};

// Syscall dispatch function
void syscall_dispatch(uint64_t num, struct trap_frame* tf);

// Individual syscall handlers
void sys_hello(void);
void sys_write(uint64_t arg0);
void sys_exit(uint64_t exit_code);
void sys_yield(void);
