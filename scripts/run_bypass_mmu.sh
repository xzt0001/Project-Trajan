#!/bin/bash
# Temporary workaround: Run kernel without MMU for basic testing
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -nographic \
  -kernel build/kernel8.img \
  -append "nommu"
