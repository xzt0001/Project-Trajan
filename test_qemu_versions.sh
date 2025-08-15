#!/bin/bash

echo "Testing older QEMU versions via Docker..."

KERNEL_PATH="/workspace/build/kernel8.img"

echo "=========================================="
echo "Test 1: Ubuntu 20.04 (QEMU 4.2 - Known Good)"
echo "=========================================="
docker run --rm -v $(pwd):/workspace ubuntu:20.04 bash -c "
    apt update -qq && apt install -y qemu-system-arm > /dev/null 2>&1
    echo 'QEMU Version:'
    qemu-system-aarch64 --version | head -1
    echo 'Testing kernel...'
    timeout 10s qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -kernel $KERNEL_PATH 2>&1 | head -15
"

echo ""
echo "=========================================="
echo "Test 2: Ubuntu 22.04 (QEMU 6.2)"
echo "=========================================="
docker run --rm -v $(pwd):/workspace ubuntu:22.04 bash -c "
    apt update -qq && apt install -y qemu-system-arm > /dev/null 2>&1
    echo 'QEMU Version:'
    qemu-system-aarch64 --version | head -1
    echo 'Testing kernel...'
    timeout 10s qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -kernel $KERNEL_PATH 2>&1 | head -15
"

echo ""
echo "=========================================="
echo "Test 3: Ubuntu 23.04 (QEMU 7.2)"
echo "=========================================="
docker run --rm -v $(pwd):/workspace ubuntu:23.04 bash -c "
    apt update -qq && apt install -y qemu-system-arm > /dev/null 2>&1
    echo 'QEMU Version:'
    qemu-system-aarch64 --version | head -1
    echo 'Testing kernel...'
    timeout 10s qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -kernel $KERNEL_PATH 2>&1 | head -15
"

echo ""
echo "Testing complete. Look for versions that show progress dots or continue past TOSPRELOOP"
