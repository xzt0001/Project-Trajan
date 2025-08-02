# Cross compiler prefix
CROSS_COMPILE := aarch64-elf-

# Tools
CC := $(CROSS_COMPILE)gcc
AS := $(CROSS_COMPILE)as
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

# Flags
CFLAGS := -Wall -O1 -ffreestanding -nostdinc -nostdlib -nostartfiles -mcpu=cortex-a53 -g
ASFLAGS := -g

# Include all the necessary files for a full kernel
OBJS := boot/start.o \
        boot/test.o \
        boot/debug_helpers.o \
        boot/security_enhanced.o \
        boot/vector_setup.o \
        boot/boot_verify.o \
        kernel/main.o \
        kernel/uart_early.o \
        kernel/uart_late.o \
        kernel/uart_legacy.o \
        kernel/uart_globals.o \
        kernel/string.o \
        kernel/test_uart_string.o \
        memory/pmm.o \
        memory/vmm.o \
        memory/memory_core.o \
        memory/memory_debug.o \
        memory/address_space.o \
        kernel/context.o \
        kernel/timer.o \
        kernel/interrupts.o \
        kernel/vector.o \
        kernel/early_trap.o \
        kernel/serror_debug_handler.o \
        kernel/task.o \
        kernel/scheduler.o \
        kernel/trap.o \
        kernel/user.o \
        kernel/syscall.o \
        kernel/user_entry.o


# C runtime is causing problems, so minimize dependencies
all: build/kernel8.img

clean:
	rm -rf build/*
	rm -f $(OBJS)
	rm -f boot/*.o kernel/*.o memory/*.o

build/kernel.elf: $(OBJS) boot/linker.ld | build
	$(LD) -T boot/linker.ld -o build/kernel.elf $(OBJS)
	$(CROSS_COMPILE)objdump -d build/kernel.elf > build/kernel.list

build/kernel8.img: build/kernel.elf | build
	$(OBJCOPY) -O binary build/kernel.elf build/kernel8.img

build:
	mkdir -p build

# Assembly files
boot/start.o: boot/start.S
	$(AS) $(ASFLAGS) boot/start.S -o boot/start.o

boot/test.o: boot/test.S
	$(AS) $(ASFLAGS) boot/test.S -o boot/test.o

boot/debug_helpers.o: boot/debug_helpers.S
	$(AS) $(ASFLAGS) boot/debug_helpers.S -o boot/debug_helpers.o

boot/security_enhanced.o: boot/security_enhanced.S
	$(AS) $(ASFLAGS) boot/security_enhanced.S -o boot/security_enhanced.o

boot/vector_setup.o: boot/vector_setup.S
	$(AS) $(ASFLAGS) boot/vector_setup.S -o boot/vector_setup.o

boot/boot_verify.o: boot/boot_verify.S
	$(AS) $(ASFLAGS) boot/boot_verify.S -o boot/boot_verify.o

kernel/context.o: kernel/context.S
	$(AS) $(ASFLAGS) kernel/context.S -o kernel/context.o

kernel/vector.o: kernel/vector.S
	$(AS) $(ASFLAGS) kernel/vector.S -o kernel/vector.o

kernel/early_trap.o: kernel/early_trap.S
	$(AS) $(ASFLAGS) kernel/early_trap.S -o kernel/early_trap.o

kernel/serror_debug_handler.o: kernel/serror_debug_handler.S
	$(AS) $(ASFLAGS) kernel/serror_debug_handler.S -o kernel/serror_debug_handler.o

# C files
kernel/main.o: kernel/main.c
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel/main.o

# UART drivers - separate early and late implementations
kernel/uart_early.o: kernel/uart_early.c
	$(CC) $(CFLAGS) -c kernel/uart_early.c -o kernel/uart_early.o

kernel/uart_late.o: kernel/uart_late.c
	$(CC) $(CFLAGS) -c kernel/uart_late.c -o kernel/uart_late.o

kernel/uart_legacy.o: kernel/uart_legacy.c
	$(CC) $(CFLAGS) -c kernel/uart_legacy.c -o kernel/uart_legacy.o

kernel/uart_globals.o: kernel/uart_globals.c
	$(CC) $(CFLAGS) -c kernel/uart_globals.c -o kernel/uart_globals.o

kernel/test_uart_string.o: kernel/test_uart_string.c
	$(CC) $(CFLAGS) -c kernel/test_uart_string.c -o kernel/test_uart_string.o

kernel/string.o: kernel/string.c
	$(CC) $(CFLAGS) -c kernel/string.c -o kernel/string.o

memory/pmm.o: memory/pmm.c
	$(CC) $(CFLAGS) -c memory/pmm.c -o memory/pmm.o

memory/vmm.o: memory/vmm.c
	$(CC) $(CFLAGS) -c memory/vmm.c -o memory/vmm.o

memory/memory_core.o: memory/memory_core.c
	$(CC) $(CFLAGS) -c memory/memory_core.c -o memory/memory_core.o

memory/memory_debug.o: memory/memory_debug.c
	$(CC) $(CFLAGS) -c memory/memory_debug.c -o memory/memory_debug.o

kernel/task.o: kernel/task.c
	$(CC) $(CFLAGS) -c kernel/task.c -o kernel/task.o

kernel/scheduler.o: kernel/scheduler.c
	$(CC) $(CFLAGS) -c kernel/scheduler.c -o kernel/scheduler.o

kernel/trap.o: kernel/trap.c
	$(CC) $(CFLAGS) -c kernel/trap.c -o kernel/trap.o

kernel/user.o: kernel/user.S
	$(AS) $(ASFLAGS) kernel/user.S -o kernel/user.o

kernel/syscall.o: kernel/syscall.c
	$(CC) $(CFLAGS) -c kernel/syscall.c -o kernel/syscall.o

kernel/user_entry.o: kernel/user_entry.c
	$(CC) $(CFLAGS) -c kernel/user_entry.c -o kernel/user_entry.o

.PHONY: all clean
