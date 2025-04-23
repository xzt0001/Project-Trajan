#!/bin/bash
# Script to analyze kernel binary format and symbols

echo "======= CHECKING IMAGE FORMAT AND SECTIONS ======="
# Get file size and type
ls -la build/kernel.elf build/kernel8.img
file build/kernel.elf build/kernel8.img

echo -e "\n======= CHECKING ELF SECTIONS ======="
# Check sections in the ELF
aarch64-elf-readelf -S build/kernel.elf | grep -E '\.text|\.text\.boot|\.text\.boot\.main'

echo -e "\n======= FINDING START AND KERNEL_MAIN SYMBOLS ======="
# Get addresses for _start and kernel_main
START_ADDR=$(aarch64-elf-nm build/kernel.elf | grep " _start$" | awk '{print $1}')
KERNEL_MAIN_ADDR=$(aarch64-elf-nm build/kernel.elf | grep " kernel_main$" | awk '{print $1}')

if [ -z "$START_ADDR" ]; then
    echo "ERROR: Could not find _start symbol!"
else
    echo "_start address: 0x$START_ADDR"
fi

if [ -z "$KERNEL_MAIN_ADDR" ]; then
    echo "ERROR: Could not find kernel_main symbol!"
else
    echo "kernel_main address: 0x$KERNEL_MAIN_ADDR"
fi

# If both found, calculate distance
if [ -n "$START_ADDR" ] && [ -n "$KERNEL_MAIN_ADDR" ]; then
    START_DEC=$((16#$START_ADDR))
    KERNEL_MAIN_DEC=$((16#$KERNEL_MAIN_ADDR))
    DISTANCE=$((KERNEL_MAIN_DEC - START_DEC))
    echo "Distance: $DISTANCE bytes ($(($DISTANCE / 1024 / 1024))MB)"
    echo "bl instruction range: ±128MB"
fi

echo -e "\n======= INSPECTING BRANCH INSTRUCTION ======="
# Check the actual branch instruction
aarch64-elf-objdump -d build/kernel.elf | grep -A2 -B2 "bl.*kernel_main" || \
aarch64-elf-objdump -d build/kernel.elf | grep -A2 -B2 "blr"

echo -e "\n======= CHECKING IMAGE GENERATION ======="
# Check command used to create the binary image
grep "kernel8.img:" -A2 /Users/xiangzhou/Desktop/Project/CustomOS/Makefile

echo -e "\n======= CHECKING VECTOR TABLE ======="
# Check vector table offset
vector_addr=$(aarch64-elf-nm build/kernel.elf | grep " vector_table$" | awk '{print $1}')
if [ -n "$vector_addr" ]; then
    echo "vector_table address: 0x$vector_addr"
    # Check alignment
    vector_dec=$((16#$vector_addr))
    align_check=$((vector_dec & 0x7FF))
    if [ $align_check -eq 0 ]; then
        echo "vector_table is 2KB aligned ✓"
    else
        echo "ERROR: vector_table is NOT 2KB aligned!"
    fi
else
    echo "ERROR: Could not find vector_table symbol!"
fi 