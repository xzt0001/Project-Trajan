#!/bin/bash

echo "Testing different QEMU configurations for MMU bug workaround..."
KERNEL="build/kernel8.img"

echo "=========================================="
echo "Test 1: Single-threaded TCG"
echo "=========================================="
timeout 15s qemu-system-aarch64 \
  -M virt -cpu cortex-a72 \
  -accel tcg,thread=single -smp 1 \
  -nographic -kernel "$KERNEL" 2>&1 | head -10

echo ""
echo "=========================================="
echo "Test 2: Multi-threaded TCG"
echo "=========================================="
timeout 15s qemu-system-aarch64 \
  -M virt -cpu cortex-a72 \
  -accel tcg,thread=multi \
  -nographic -kernel "$KERNEL" 2>&1 | head -10

echo ""
echo "=========================================="
echo "Test 3: Default acceleration"
echo "=========================================="
timeout 15s qemu-system-aarch64 \
  -M virt -cpu cortex-a72 \
  -nographic -kernel "$KERNEL" 2>&1 | head -10

echo ""
echo "=========================================="
echo "Test 4: Raspberry Pi 3B emulation"
echo "=========================================="
timeout 15s qemu-system-aarch64 \
  -M raspi3b -cpu cortex-a72 \
  -nographic -kernel "$KERNEL" 2>&1 | head -10

echo ""
echo "Testing complete."
