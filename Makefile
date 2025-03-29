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
        kernel/main.o \
        kernel/uart.o \
        kernel/string.o \
        memory/pmm.o \
        memory/vmm.o

# C runtime is causing problems, so minimize dependencies
all: build/kernel8.img

clean:
	rm -rf build/*
	rm -f $(OBJS)

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

# C files
kernel/main.o: kernel/main.c
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel/main.o

kernel/uart.o: kernel/uart.c
	$(CC) $(CFLAGS) -c kernel/uart.c -o kernel/uart.o

kernel/string.o: kernel/string.c
	$(CC) $(CFLAGS) -c kernel/string.c -o kernel/string.o

memory/pmm.o: memory/pmm.c
	$(CC) $(CFLAGS) -c memory/pmm.c -o memory/pmm.o

memory/vmm.o: memory/vmm.c
	$(CC) $(CFLAGS) -c memory/vmm.c -o memory/vmm.o

.PHONY: all clean
