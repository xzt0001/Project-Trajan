#!/bin/bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -serial stdio \
  -monitor telnet:127.0.0.1:5555,server,nowait \
  -display none \
  -kernel build/kernel8.img

echo "Monitor available on telnet port 5555"
