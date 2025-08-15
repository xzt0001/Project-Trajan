#!/bin/bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -accel tcg,thread=single \
  -smp 1 \
  -nographic \
  -kernel build/kernel8.img
