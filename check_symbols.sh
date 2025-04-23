#!/bin/bash
# Check for kernel_main symbol in the binary

echo "Checking for kernel_main symbol in kernel.elf..."
aarch64-elf-nm build/kernel.elf | grep kernel_main

echo "Checking entry point and section info..."
aarch64-elf-readelf -h build/kernel.elf | grep Entry
aarch64-elf-readelf -S build/kernel.elf | grep -E ".text|.text.boot"

echo "Examining bl instruction to kernel_main..."
aarch64-elf-objdump -d build/kernel.elf | grep -A2 -B2 "bl.*kernel_main"

echo "Complete symbol table:"
aarch64-elf-nm build/kernel.elf | sort 