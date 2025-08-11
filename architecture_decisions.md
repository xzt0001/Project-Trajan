***This document records major architectural decisions made in the development of this OS. The purpose is: Document the reasoning, context, and tradeoffs behind critical changes, including past and future, as project evolves.

August 10th 2025

Kernel Folder Reorganization:

kernel/
├── arch/arm64/           // Architecture-specific ARM64 code
│   ├── boot/            // Early boot and low-level initialization
│   │   ├── vector.S     // Exception vector table
│   │   ├── early_trap.S // Early trap handling
│   │   └── main_asm.S   // Assembly kernel entry (excluded - conflicts)
│   ├── kernel/          // Core ARM64 kernel operations
│   │   ├── context.S    // Context switching
│   │   ├── user.S       // User space transitions
│   │   ├── user_task.S  // User task management
│   │   └── serror_debug_handler.S // Error handling
│   ├── lib/             // ARM64-optimized utilities
│   │   └── string.c     // String operations
│   └── mm/              // (Future) ARM64 memory management
├── core/                // Architecture-independent kernel core
│   ├── sched/           // Process scheduling
│   │   └── scheduler.c  // Main scheduler implementation
│   ├── syscall/         // System call handling
│   │   ├── syscall.c    // System call dispatcher
│   │   └── trap.c       // Trap handling
│   ├── irq/             // Interrupt handling
│   │   ├── interrupts.c // Interrupt management
│   │   └── irq.c        // IRQ processing
│   └── task/            // Task management
│       ├── task.c       // Task creation/management
│       ├── user_entry.c // User space entry points
│       └── user_stub.c  // User space stubs
├── drivers/             // Device drivers
│   ├── uart/            // UART serial communication
│   │   ├── uart_core.c  // Core UART functionality
│   │   ├── uart_late.c  // Late-stage UART operations
│   │   └── uart_globals.c // UART global variables
│   └── timer/           // Timer/clock drivers
│       └── timer.c      // Timer implementation
├── init/                // Kernel initialization
│   ├── arch/            // Architecture-related init helpers
│   │   ├── vector_ops.c // Exception vector setup helpers
│   │   └── vbar_ops.c   // VBAR configuration helpers
│   ├── console/         // Early console setup
│   │   └── early_console.c
│   ├── core/            // Core init utilities
│   │   └── panic.c      // Panic & fatal error handling
│   ├── include/         // Init-time public headers
│   │   ├── arch_ops.h
│   │   ├── console_api.h
│   │   ├── memory_debug.h
│   │   ├── panic.h
│   │   ├── sample_tasks.h
│   │   └── selftest.h
│   ├── memory/          // Memory init & debug
│   │   └── debug_ptdump.c
│   ├── samples/         // Demo workloads used during bring-up
│   │   └── demo_tasks.c
│   ├── selftest/        // Built-in self tests
│   │   ├── exception_tests.c
│   │   ├── scheduler_tests.c
│   │   └── uart_tests.c
│   └── main.c           // Main kernel entry point
├── lib/                 // Kernel utility libraries
└── debug/               // Debug and testing utilities
    ├── test_uart_string.c
    ├── simple_main.c
    ├── ultra_simple_main.c
    └── minimal_test.c


July 11th 2025

vmm.c refactoring plan: vmm.c to a modular memory subsystem

memory/
├── pmm.c              // Physical Memory Manager
├── vmm.c              // Virtual Memory Manager (MMU-dependent)
├── memory_core.c      // Common utilities & hardware access
├── address_space.c    // Addressing abstraction layer
├── memory_config.h    // Runtime configuration switches
└── memory_debug.c     // Debugging & verification tools