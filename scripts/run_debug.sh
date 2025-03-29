#!/bin/bash

echo "Launching QEMU in debug mode (GDB on port 1234)..."

qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -nographic \
  -s -S \
  -kernel build/kernel8.img
