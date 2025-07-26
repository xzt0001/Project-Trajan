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
CustomOS
├── boot/
│   ├── linker.ld            # Kernel linker script
│   └── start.S              # Boot assembly code
├── kernel/
│   ├── context.S            # Context switching code
│   ├── interrupts.c         # Interrupt handling
│   ├── main.c               # Kernel entry point
│   ├── scheduler.c          # Task scheduling
│   ├── serror_debug_handler.S # System error debug handler
│   ├── string.c             # String manipulation utilities
│   ├── syscall.c            # System call implementation
│   ├── task.c               # Task management
│   ├── timer.c              # System timer implementation
│   ├── trap.c               # Exception handlers
│   ├── uart_early.c         # Early boot UART driver
│   ├── uart_late.c          # Post-MMU UART driver
│   ├── user.S               # User mode support
│   ├── user_entry.c         # User task entry points
│   └── vector.S             # Exception vector table
├── memory/
│   ├── pmm.c                # Physical memory manager
│   └── vmm.c                # Virtual memory manager
├── include/
│   ├── debug.h              # Debugging utilities
│   ├── interrupts.h         # Interrupt declarations
│   ├── kernel.h             # Kernel-wide definitions
│   ├── pmm.h                # Physical memory declarations
│   ├── scheduler.h          # Scheduler declarations
│   ├── string.h             # String utilities
│   ├── syscall.h            # System call definitions
│   ├── task.h               # Task management declarations
│   ├── timer.h              # Timer declarations
│   ├── types.h              # Common type definitions
│   ├── uart.h               # UART driver interface
│   └── vmm.h                # Virtual memory declarations
└── scripts/
    ├── run_debug.sh         # Run with GDB debugging
    ├── run_gui_mode.sh      # Run with QEMU GUI
    ├── run_monitor_telnet.sh # Run with monitor over telnet
    ├── run_nographic.sh     # Run in nographic mode
    ├── run_serial_file.sh   # Run with serial output to file
    └── run_serial_stdio.sh  # Run with serial output to stdio
