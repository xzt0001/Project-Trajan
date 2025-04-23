# CustomOS: A Minimal ARM64 Kernel

A clean, minimal operating system kernel written from scratch for AArch64 (ARMv8-A) using QEMU.  
Includes UART logging, stack setup, physical memory management, virtual memory mapping, MMU activation, context switching, and exception handling.

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
We added comprehensive logging at critical transition points, capturing:
- Register state before/after MMU enablement
- Page table entry validity
- Memory mapping verification
- Stack alignment checks
- Executable permission verification

### 2. Hypothesis-Driven Debugging
We identified three potential failure points:
- Vector table accessibility post-MMU enablement
- Program counter translation continuity
- Exception handling race conditions

### 3. Controlled Experiments
For each hypothesis, we implemented targeted solutions:
- Direct mapping of vector table to known virtual address
- Identity mapping of transition code
- VBAR_EL1 update sequencing

### 4. Verification
We verified each solution through:
- Memory permission bit inspection
- Register state validation
- Execution continuity testing

This methodical approach allowed us to solve one of ARM64's most challenging bootstrapping problems: maintaining execution flow while fundamentally changing the memory addressing model.