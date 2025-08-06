# Project 500: A Minimal ARM64 Kernel

A clean, minimal operating system kernel written from scratch for AArch64 (ARMv8-A) using QEMU.  
Includes UART logging, stack setup, physical memory management, virtual memory mapping, MMU activation, context switching, and exception handling.


**Current Status: This project is currently in its foundational development stage. The Philosophy.md document establishes the intellectual framework and long-term vision, while the current codebase builds the essential technical infrastructure needed to eventually demonstrate these concepts.

**Researchers interested in adversarial OS design, trust boundaries, or system subversion are invited to initiate technical dialogue or collaboration.


For debugging blogs, Q&A, please visit: https://medium.com/@xzt0202 


Future Roadmap(generalized version):

Phase 4: File System and Storage
	•	Implement a RAM-based filesystem (tmpfs-like).
	•	Add support for basic file operations (open, read, write, close).
	•	Begin building a persistent storage backend (simple EXT2-like structures).

Phase 5: User Management and Process Isolation
	•	Implement user authentication and account management.
	•	Enforce user-space vs kernel-space privilege separation.
	•	Strengthen process isolation to prevent unauthorized memory access.

Phase 6: Networking and Basic Security
	•	Build a basic TCP/IP networking stack.
	•	Add rudimentary network encryption for communication security.
	•	Prepare infrastructure for multi-process and remote interactions.


Research Platform Expansion(The content of this section is temporarily unavailable)


Long-Term Vision

This platform will serve as:
	•	A low-level systems security research environment.
	•	A teaching and experimentation tool for OS security concepts.
	•	A reproducible framework for studying real-world attack models and defense strategies.

Work will continue toward memory integrity enforcement, syscall verification, containment protocols, and formal verification of critical subsystems.


## Project Structure
```
Project 500
├── boot/
│   ├── boot_verify.S           # Boot verification assembly
│   ├── debug_helpers.S         # Debug helper assembly routines
│   ├── linker.ld               # Kernel linker script
│   ├── minimal_test.S          # Minimal boot test
│   ├── security_enhanced.S     # Security-enhanced boot code
│   ├── start_simple.S          # Simple start assembly
│   ├── start.S                 # Boot assembly code
│   ├── test.S                  # Boot test code
│   └── vector_setup.S          # Exception vector setup
├── kernel/                     # Modular ARM64 kernel (reorganized August 2025)
│   ├── arch/arm64/             # Architecture-specific ARM64 code
│   │   ├── boot/               # Early boot and low-level initialization
│   │   │   ├── vector.S        # Exception vector table
│   │   │   ├── early_trap.S    # Early trap handling
│   │   │   └── main_asm.S      # Assembly kernel entry (excluded)
│   │   ├── kernel/             # Core ARM64 kernel operations
│   │   │   ├── context.S       # Context switching
│   │   │   ├── user.S          # User space transitions
│   │   │   ├── user_task.S     # User task management
│   │   │   └── serror_debug_handler.S # Error handling
│   │   └── lib/                # ARM64-optimized utilities
│   │       └── string.c        # String operations
│   ├── core/                   # Architecture-independent kernel core
│   │   ├── sched/              # Process scheduling
│   │   │   └── scheduler.c     # Main scheduler implementation
│   │   ├── syscall/            # System call handling
│   │   │   ├── syscall.c       # System call dispatcher
│   │   │   └── trap.c          # Trap handling
│   │   ├── irq/                # Interrupt handling
│   │   │   ├── interrupts.c    # Interrupt management
│   │   │   └── irq.c           # IRQ processing
│   │   └── task/               # Task management
│   │       ├── task.c          # Task creation/management
│   │       ├── user_entry.c    # User space entry points
│   │       └── user_stub.c     # User space stubs
│   ├── drivers/                # Device drivers
│   │   ├── uart/               # UART serial communication
│   │   │   ├── uart_core.c     # Core UART functionality
│   │   │   ├── uart_late.c     # Late-stage UART operations
│   │   │   └── uart_globals.c  # UART global variables
│   │   └── timer/              # Timer/clock drivers
│   │       └── timer.c         # Timer implementation
│   ├── init/                   # Kernel initialization
│   │   └── main.c              # Main kernel entry point
│   └── debug/                  # Debug and testing utilities
│       ├── test_uart_string.c  # UART string test
│       ├── simple_main.c       # Simple kernel main
│       ├── ultra_simple_main.c # Ultra simple kernel main
│       └── minimal_test.c      # Minimal kernel test
├── memory/
│   ├── address_space.c         # Address space management
│   ├── memory_core.c           # Core memory management
│   ├── memory_debug.c          # Memory debugging utilities
│   ├── memory_debug.h          # Memory debug header
│   ├── pmm.c                   # Physical memory manager
│   └── vmm.c                   # Virtual memory manager
├── include/
│   ├── address_space.h         # Address space declarations
│   ├── debug.h                 # Debugging utilities
│   ├── interrupts.h            # Interrupt declarations
│   ├── kernel.h                # Kernel-wide definitions
│   ├── memory_config.h         # Memory configuration
│   ├── memory_core.h           # Core memory management declarations
│   ├── pmm.h                   # Physical memory declarations
│   ├── scheduler.h             # Scheduler declarations
│   ├── string.h                # String utilities
│   ├── syscall.h               # System call definitions
│   ├── task.h                  # Task management declarations
│   ├── timer.h                 # Timer declarations
│   ├── types.h                 # Common type definitions
│   ├── uart.h                  # UART driver interface
│   └── vmm.h                   # Virtual memory declarations
└── scripts/
    ├── run_debug.sh         # Run with GDB debugging
    ├── run_gui_mode.sh      # Run with QEMU GUI
    ├── run_monitor_telnet.sh # Run with monitor over telnet
    ├── run_nographic.sh     # Run in nographic mode
    ├── run_serial_file.sh   # Run with serial output to file
    └── run_serial_stdio.sh  # Run with serial output to stdio
