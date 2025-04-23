#!/bin/bash
# Debug script for CustomOS

echo "=== Cleaning and building the project ==="
make clean
make

if [ $? -ne 0 ]; then
    echo "Build failed! Fix compilation errors first."
    exit 1
fi

echo "=== Build successful! ==="
echo "Starting QEMU with GDB server enabled"

# Run QEMU in the background
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -nographic \
  -kernel build/kernel8.img \
  -S -gdb tcp::1234 \
  -d int,cpu_reset,guest_errors,unimp &

QEMU_PID=$!

echo "QEMU started with PID: $QEMU_PID"
echo "UART output will appear in this terminal"
echo ""
echo "Starting GDB in a new terminal..."

# For macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # Open a new terminal window with GDB
    osascript -e 'tell application "Terminal" to do script "cd '$PWD' && aarch64-elf-gdb -x debug_kernel.gdb build/kernel.elf"'
else
    # For Linux
    gnome-terminal -- bash -c "cd $PWD && aarch64-elf-gdb -x debug_kernel.gdb build/kernel.elf"
fi

echo "Waiting for QEMU to exit (press Ctrl+C to terminate)"
wait $QEMU_PID 