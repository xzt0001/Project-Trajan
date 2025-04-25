# CustomOS: A Minimal ARM64 Kernel

A clean, minimal operating system kernel written from scratch for AArch64 (ARMv8-A) using QEMU.  
Includes UART logging, stack setup, physical memory management, virtual memory mapping, MMU activation, context switching, and exception handling.

Future Roadmap:

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
│   ├── linker.ld
│   └── start.S
├── kernel/
│   ├── context.S      # Context switching code
│   ├── main.c         # Kernel entry point
│   ├── scheduler.c    # Task scheduling
│   ├── trap.c         # Exception handlers
│   ├── uart.c         # Serial console
│   └── vector.S       # Exception vector table
├── memory/
│   ├── pmm.c          # Physical memory manager
│   └── vmm.c          # Virtual memory manager
└── include/
    ├── uart.h
    ├── pmm.h
    ├── vmm.h
    └── task.h
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

## Debugging Methodology: MMU Enablement Case Study

My approach to debugging the complex MMU transition problem demonstrates the systematic methodology used throughout this project:

### 1. Problem Instrumentation
Added comprehensive logging at critical transition points, capturing:
- Register state before/after MMU enablement
- Page table entry validity
- Memory mapping verification
- Stack alignment checks
- Executable permission verification

### 2. Hypothesis-Driven Debugging
Identified three potential failure points:
- Vector table accessibility post-MMU enablement
- Program counter translation continuity
- Exception handling race conditions

### 3. Controlled Experiments
For each hypothesis, I implemented targeted solutions:
- Direct mapping of vector table to known virtual address
- Identity mapping of transition code
- VBAR_EL1 update sequencing

### 4. Verification
Verified each solution through:
- Memory permission bit inspection
- Register state validation
- Execution continuity testing

This methodical approach allowed me to solve one of ARM64's most challenging bootstrapping problems: maintaining execution flow while fundamentally changing the memory addressing model.