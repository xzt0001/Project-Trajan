#!/bin/bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -serial file:uart.log \
  -monitor none \
  -display none \
  -kernel build/kernel8.img

echo "UART output saved to uart.log"
