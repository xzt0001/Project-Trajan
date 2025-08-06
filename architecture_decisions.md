***This document records major architectural decisions made in the development of this OS. The purpose is: Document the reasoning, context, and tradeoffs behind critical changes, including past and future, as project evolves.

July 11th 2025

vmm.c refactoring plan: vmm.c to a modular memory subsystem

memory/
├── pmm.c              // Physical Memory Manager
├── vmm.c              // Virtual Memory Manager (MMU-dependent)
├── memory_core.c      // Common utilities & hardware access
├── address_space.c    // Addressing abstraction layer
├── memory_config.h    // Runtime configuration switches
└── memory_debug.c     // Debugging & verification tools

August 6th, 2025

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
│   └── main.c           // Main kernel entry point
├── lib/                 // Kernel utility libraries
└── debug/               // Debug and testing utilities
    ├── test_uart_string.c
    ├── simple_main.c
    ├── ultra_simple_main.c
    └── minimal_test.c


