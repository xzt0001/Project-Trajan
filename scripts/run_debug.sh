#!/bin/bash
# Run QEMU with GDB debugging enabled

echo "Starting QEMU with GDB server on port 1234..."
echo "To connect with GDB, open another terminal and run:"
echo "cd $(pwd) && aarch64-elf-gdb -x debug_commands.gdb build/kernel.elf"

# Start QEMU with GDB server - use -S to pause at startup
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -nographic \
  -kernel build/kernel8.img \
  -S -gdb tcp::1234 \
  -d int,cpu_reset,guest_errors,unimp

# Wait for QEMU to exit
echo "QEMU has terminated."
