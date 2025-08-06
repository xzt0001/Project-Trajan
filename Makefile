# Cross compiler prefix
CROSS_COMPILE := aarch64-elf-

# Tools
CC := $(CROSS_COMPILE)gcc
AS := $(CROSS_COMPILE)as
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

# Debug configuration - uncomment one of these lines:
# For full verbose debug output (development):
DEBUG_FLAGS := -DDEBUG_BOOT_VERBOSE
# For moderate debug output (testing):
# DEBUG_FLAGS := -DDEBUG_BOOT_MODERATE  
# For minimal debug output (production):
# DEBUG_FLAGS := -DDEBUG_BOOT_MINIMAL
# For no debug output (release):
# DEBUG_FLAGS := -DDEBUG_BOOT_SILENT

# Flags
CFLAGS := -Wall -O1 -ffreestanding -nostdinc -nostdlib -nostartfiles -mcpu=cortex-a53 -g $(DEBUG_FLAGS)
ASFLAGS := -g

# New modular object files structure
BOOT_OBJS := boot/start.o \
             boot/test.o \
             boot/debug_helpers.o \
             boot/security_enhanced.o \
             boot/vector_setup.o \
             boot/boot_verify.o

ARCH_ARM64_BOOT_OBJS := kernel/arch/arm64/boot/vector.o \
                        kernel/arch/arm64/boot/early_trap.o

ARCH_ARM64_KERNEL_OBJS := kernel/arch/arm64/kernel/context.o \
                          kernel/arch/arm64/kernel/user.o \
                          kernel/arch/arm64/kernel/user_task.o \
                          kernel/arch/arm64/kernel/serror_debug_handler.o

ARCH_ARM64_LIB_OBJS := kernel/arch/arm64/lib/string.o

CORE_SCHED_OBJS := kernel/core/sched/scheduler.o

CORE_SYSCALL_OBJS := kernel/core/syscall/syscall.o \
                     kernel/core/syscall/trap.o

CORE_IRQ_OBJS := kernel/core/irq/interrupts.o \
                 kernel/core/irq/irq.o

CORE_TASK_OBJS := kernel/core/task/task.o \
                  kernel/core/task/user_entry.o \
                  kernel/core/task/user_stub.o

DRIVERS_UART_OBJS := kernel/drivers/uart/uart_core.o \
                     kernel/drivers/uart/uart_late.o \
                     kernel/drivers/uart/uart_globals.o

DRIVERS_TIMER_OBJS := kernel/drivers/timer/timer.o

INIT_OBJS := kernel/init/main.o

MEMORY_OBJS := memory/pmm.o \
               memory/vmm.o \
               memory/memory_core.o \
               memory/memory_debug.o \
               memory/address_space.o

# Combine all object files
OBJS := $(BOOT_OBJS) \
        $(ARCH_ARM64_BOOT_OBJS) \
        $(ARCH_ARM64_KERNEL_OBJS) \
        $(ARCH_ARM64_LIB_OBJS) \
        $(CORE_SCHED_OBJS) \
        $(CORE_SYSCALL_OBJS) \
        $(CORE_IRQ_OBJS) \
        $(CORE_TASK_OBJS) \
        $(DRIVERS_UART_OBJS) \
        $(DRIVERS_TIMER_OBJS) \
        $(INIT_OBJS) \
        $(MEMORY_OBJS)

# C runtime is causing problems, so minimize dependencies
all: build/kernel8.img

# Debug build targets for different verbosity levels
debug-verbose: clean
	$(MAKE) DEBUG_FLAGS="-DDEBUG_BOOT_VERBOSE" all
	@echo "=== Built with VERBOSE debug output ==="

debug-moderate: clean  
	$(MAKE) DEBUG_FLAGS="-DDEBUG_BOOT_MODERATE" all
	@echo "=== Built with MODERATE debug output ==="

debug-minimal: clean
	$(MAKE) DEBUG_FLAGS="-DDEBUG_BOOT_MINIMAL" all
	@echo "=== Built with MINIMAL debug output ==="

debug-silent: clean
	$(MAKE) DEBUG_FLAGS="-DDEBUG_BOOT_SILENT" all  
	@echo "=== Built with NO debug output ==="

clean:
	rm -rf build/*
	rm -f $(OBJS)
	rm -f boot/*.o kernel/arch/arm64/boot/*.o kernel/arch/arm64/kernel/*.o kernel/arch/arm64/lib/*.o
	rm -f kernel/core/sched/*.o kernel/core/syscall/*.o kernel/core/irq/*.o kernel/core/task/*.o
	rm -f kernel/drivers/uart/*.o kernel/drivers/timer/*.o kernel/init/*.o memory/*.o

build/kernel.elf: $(OBJS) boot/linker.ld | build
	$(LD) -T boot/linker.ld -o build/kernel.elf $(OBJS)
	$(CROSS_COMPILE)objdump -d build/kernel.elf > build/kernel.list

build/kernel8.img: build/kernel.elf | build
	$(OBJCOPY) -O binary build/kernel.elf build/kernel8.img

build:
	mkdir -p build

# ========== BOOT ASSEMBLY FILES ==========
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

# ========== ARCH ARM64 BOOT FILES ==========
kernel/arch/arm64/boot/vector.o: kernel/arch/arm64/boot/vector.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/boot/vector.S -o kernel/arch/arm64/boot/vector.o

kernel/arch/arm64/boot/early_trap.o: kernel/arch/arm64/boot/early_trap.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/boot/early_trap.S -o kernel/arch/arm64/boot/early_trap.o

# Excluded: kernel/arch/arm64/boot/main_asm.o (conflicts with main.c)

# ========== ARCH ARM64 KERNEL FILES ==========
kernel/arch/arm64/kernel/context.o: kernel/arch/arm64/kernel/context.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/kernel/context.S -o kernel/arch/arm64/kernel/context.o

kernel/arch/arm64/kernel/user.o: kernel/arch/arm64/kernel/user.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/kernel/user.S -o kernel/arch/arm64/kernel/user.o

kernel/arch/arm64/kernel/user_task.o: kernel/arch/arm64/kernel/user_task.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/kernel/user_task.S -o kernel/arch/arm64/kernel/user_task.o

kernel/arch/arm64/kernel/serror_debug_handler.o: kernel/arch/arm64/kernel/serror_debug_handler.S
	$(AS) $(ASFLAGS) kernel/arch/arm64/kernel/serror_debug_handler.S -o kernel/arch/arm64/kernel/serror_debug_handler.o

# ========== ARCH ARM64 LIB FILES ==========
kernel/arch/arm64/lib/string.o: kernel/arch/arm64/lib/string.c
	$(CC) $(CFLAGS) -c kernel/arch/arm64/lib/string.c -o kernel/arch/arm64/lib/string.o

# ========== CORE SCHEDULER FILES ==========
kernel/core/sched/scheduler.o: kernel/core/sched/scheduler.c
	$(CC) $(CFLAGS) -c kernel/core/sched/scheduler.c -o kernel/core/sched/scheduler.o

# ========== CORE SYSCALL FILES ==========
kernel/core/syscall/syscall.o: kernel/core/syscall/syscall.c
	$(CC) $(CFLAGS) -c kernel/core/syscall/syscall.c -o kernel/core/syscall/syscall.o

kernel/core/syscall/trap.o: kernel/core/syscall/trap.c
	$(CC) $(CFLAGS) -c kernel/core/syscall/trap.c -o kernel/core/syscall/trap.o

# ========== CORE IRQ FILES ==========
kernel/core/irq/interrupts.o: kernel/core/irq/interrupts.c
	$(CC) $(CFLAGS) -c kernel/core/irq/interrupts.c -o kernel/core/irq/interrupts.o

kernel/core/irq/irq.o: kernel/core/irq/irq.c
	$(CC) $(CFLAGS) -c kernel/core/irq/irq.c -o kernel/core/irq/irq.o

# ========== CORE TASK FILES ==========
kernel/core/task/task.o: kernel/core/task/task.c
	$(CC) $(CFLAGS) -c kernel/core/task/task.c -o kernel/core/task/task.o

kernel/core/task/user_entry.o: kernel/core/task/user_entry.c
	$(CC) $(CFLAGS) -c kernel/core/task/user_entry.c -o kernel/core/task/user_entry.o

kernel/core/task/user_stub.o: kernel/core/task/user_stub.c
	$(CC) $(CFLAGS) -c kernel/core/task/user_stub.c -o kernel/core/task/user_stub.o

# ========== UART DRIVER FILES ==========
kernel/drivers/uart/uart_core.o: kernel/drivers/uart/uart_core.c
	$(CC) $(CFLAGS) -c kernel/drivers/uart/uart_core.c -o kernel/drivers/uart/uart_core.o

# Excluded: kernel/drivers/uart/uart_early.o (conflicts with uart_core.c)

kernel/drivers/uart/uart_late.o: kernel/drivers/uart/uart_late.c
	$(CC) $(CFLAGS) -c kernel/drivers/uart/uart_late.c -o kernel/drivers/uart/uart_late.o

# Excluded: kernel/drivers/uart/uart_legacy.o (conflicts with uart_core.c)

kernel/drivers/uart/uart_globals.o: kernel/drivers/uart/uart_globals.c
	$(CC) $(CFLAGS) -c kernel/drivers/uart/uart_globals.c -o kernel/drivers/uart/uart_globals.o

# ========== TIMER DRIVER FILES ==========
kernel/drivers/timer/timer.o: kernel/drivers/timer/timer.c
	$(CC) $(CFLAGS) -c kernel/drivers/timer/timer.c -o kernel/drivers/timer/timer.o

# ========== INIT FILES ==========
kernel/init/main.o: kernel/init/main.c
	$(CC) $(CFLAGS) -c kernel/init/main.c -o kernel/init/main.o

# ========== MEMORY MANAGEMENT FILES ==========
memory/pmm.o: memory/pmm.c
	$(CC) $(CFLAGS) -c memory/pmm.c -o memory/pmm.o

memory/vmm.o: memory/vmm.c
	$(CC) $(CFLAGS) -c memory/vmm.c -o memory/vmm.o

memory/memory_core.o: memory/memory_core.c
	$(CC) $(CFLAGS) -c memory/memory_core.c -o memory/memory_core.o

memory/memory_debug.o: memory/memory_debug.c
	$(CC) $(CFLAGS) -c memory/memory_debug.c -o memory/memory_debug.o

memory/address_space.o: memory/address_space.c
	$(CC) $(CFLAGS) -c memory/address_space.c -o memory/address_space.o

.PHONY: all clean