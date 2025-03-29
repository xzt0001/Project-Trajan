#!/bin/bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -serial stdio \
  -kernel build/kernel8.img
