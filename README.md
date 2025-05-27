# CustomOS: A Minimal ARM64 Kernel

A clean, minimal operating system kernel written from scratch for AArch64 (ARMv8-A) using QEMU.  
Includes UART logging, stack setup, physical memory management, virtual memory mapping, MMU activation, context switching, and exception handling.


**Current Status: This project is currently in its foundational development stage. The Philosophy.md document establishes the intellectual framework and long-term vision, while the current codebase builds the essential technical infrastructure needed to eventually demonstrate these concepts.


To those who need clarification: This platform is for simulation only, not deployment.


For debugging blogs, Q&A, please visit: https://medium.com/@xzt0202 

I can see my project has gained a significant amount of traffic for the past few days. Below is a self directed Q&A to address the most common question you might wonder, for in-depth detailed debugging write-ups and more detailed Q&A, please visit the medium link above. 


Why are you doing this OS project in the first place? 

Answer: I developed a strong interest in malware that operates at the kernel level since I learned about Pegasus story, especially when it come to behaviors like privilege escalation, trap hijacking, and stealth persistence across MMU transitions. So I decided to build my own OS from the ground up with the long term goal of simulation Pegasus style APT at the kernel level without raising ethical concerns.

That said, the immediate goal, and top priority for the moment would be mastering the internal workings of operating systems at the lowest level through hands on development. 


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


Research Platform Expansion

Pegasus-Class APT Simulation and Defense

This project will evolve into a kernel-level research testbed capable of simulating Pegasus-style Advanced Persistent Threats (APTs) within a custom-built ARM64 operating system.

Phase 2A: Implant Simulation
	•	Simulate stealth implant delivery via syscall injection or rogue process spawning.
	•	Model privilege escalation via corrupted syscall tables or page mappings.
	•	Build persistence mechanisms across reboots via injected hooks or init hijacking.
	•	Emulate data exfiltration channels through UART and memory scanning.
	•	Implement anti-forensics techniques such as self-deletion and memory unlinking.

Phase 2B: OS-Level Defenses
	•	Syscall Trace Monitor to detect anomalous syscall flows.
	•	Memory Integrity Zones enforced via page table protections and MMU traps.
	•	Secure Logging Kernel (append-only in-memory logs protected against tampering).
	•	Heuristic-based behavioral engine to detect suspicious transitions and persistence attempts.
	•	Emergency Containment Protocols to isolate compromised processes or trigger safe reboots.

Phase 2C: Experimental Evaluation
	•	Attack Chain Graphs to visualize multi-stage APT behavior (drop → escalate → persist → exfiltrate).
	•	Threat Model document describing attacker assumptions and TCB boundaries.
	•	Evaluation Matrix to measure detection speed, containment time, and system impact.
	•	Research Abstract framing this work for potential workshop or conference submission.


Long-Term Vision

The CustomOS platform will serve as:
	•	A low-level systems security research environment.
	•	A teaching and experimentation tool for OS security concepts.
	•	A reproducible framework for studying real-world attack models and defense strategies.

Work will continue toward memory integrity enforcement, syscall verification, containment protocols, and formal verification of critical subsystems.

## Features

### Core Architecture
- Bootable from QEMU `virt` machine using `kernel8.img`
- Custom linker script and assembly bootstrap (`start.S`)
- Stack setup with proper alignment and mapping
- UART driver (PL011) with output before and after MMU enablement

### Memory Management
- Physical page allocator (bitmap-backed)
- Virtual memory manager with full L0–L3 page table support
- **MMU enablement with robust transition handling:**
  - Identity mapping for execution continuity
  - Vector table mapping with VBAR_EL1 pre-update
  - Executable permission (PXN/UXN) management
  - Safe context switching between address spaces

### Task Management
- Task structure with register state storage
- Context switching with full register state preservation
- Supervisor call (SVC) handling for system services
- EL0 (user mode) task support

### Debugging Infrastructure
- Comprehensive exception handling with diagnostic output
- Debug logging via UART with formatted hex output
- Memory permission verification
- Stack alignment checking

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
```

## 🛠 Build Instructions

### Dependencies
- `qemu-system-aarch64`
- `aarch64-elf-gcc` cross toolchain

### Build and Run

```bash
make
qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel build/kernel8.img
```


## Recent Fixes and Improvements

### MMU UART String Output Fix

The UART string output issues after MMU enablement have been resolved with a comprehensive approach:

1. **Global String Buffers**: Added global static buffers (aligned to cache lines) for safe string storage during MMU transition.

2. **Improved Cache Maintenance**: Implemented explicit cache line cleaning/invalidation with proper barriers for UART strings.

3. **TLB Invalidation**: Fixed TLB invalidation with proper inner-shareable domain instructions (`tlbi vmalle1is` instead of `tlbi vmalle1`).

4. **Memory Barriers**: Enhanced synchronization with proper barriers (DSB ISH, ISB) across all critical MMU transitions.

5. **Emergency UART Functions**: Added assembly-based UART access functions that bypass normal C code hazards.

6. **Hardened String Handling**: Modified string output functions to use global buffers and guard against dereference hazards.

7. **UART MMIO Mapping**: Improved UART device memory mapping with explicit cache maintenance and TLB invalidation.

8. **Diagnostic Capabilities**: Added extensive diagnostic output during the MMU transition for debugging.



## Build Instructions

```
make clean
make
```

## Running

The OS can be run on the Raspberry Pi 3/4 or in QEMU.

### QEMU

For QEMU with debugging:

```
qemu-system-aarch64 -M raspi3 -kernel build/kernel8.img -serial stdio -s -S
```

For QEMU without debugging:

```
qemu-system-aarch64 -M raspi3 -kernel build/kernel8.img -serial stdio
```

```
aarch64-elf-gdb build/kernel.elf -ex "target remote localhost:1234"
```