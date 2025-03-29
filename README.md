# CustomOS: A Minimal ARM64 Kernel

A clean, minimal operating system kernel written from scratch for AArch64 (ARMv8-A) using QEMU.  
Includes UART logging, stack setup, physical memory management, virtual memory mapping, and MMU activation.

## Features (Phases 1 & 2)
- Bootable from QEMU `virt` machine using `kernel8.img`
- Custom linker script and assembly bootstrap (`start.S`)
- Stack setup with proper alignment and mapping
- UART driver (PL011) with output tested before and after MMU
- Physical page allocator (bitmap-backed)
- Virtual memory manager with full L0–L3 page table setup
- MMU activation and testing with read/write validation
- Debug logging via UART using formatted hex and output control
- Clean and modular C codebase, compatible with `-ffreestanding`

Structure:
CustomOS
- boot
    - linker.ld
    - start.S
- kernel
    - main.c
    - string.c
    - uart.c
- memory
    - pmm.c
    - vmm.c
- include
    - uart.h
    - pmm.h
- build
- scripts
-  Makefile
- README.md

## 🛠 Build Instructions

### Dependencies
- `qemu-system-aarch64`
- `aarch64-elf-gcc` cross toolchain

### Build and Run

```bash
make
qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel build/kernel8.img