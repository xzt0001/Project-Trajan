#!/usr/bin/env python3
# GDB Python script to debug kernel.elf in QEMU
# Usage: gdb -x debug_gdb.py

import gdb
import os
import time

# Load the ELF file
gdb.execute("file build/kernel.elf")

# Connect to QEMU's gdbserver
gdb.execute("target remote localhost:1234")

# Set breakpoint at _start
gdb.execute("break _start")
print("Breakpoint set at _start")

# Set breakpoint at kernel_main
gdb.execute("break kernel_main")
print("Breakpoint set at kernel_main")

# Continue to _start
gdb.execute("continue")
print("\n===== Reached _start =====")

# Print register state at _start
gdb.execute("info registers")

# Continue to kernel_main
print("\nContinuing to kernel_main...")
gdb.execute("continue")

# Check if we reached kernel_main
try:
    frame_info = gdb.execute("frame", to_string=True)
    if "kernel_main" in frame_info:
        print("\n===== Reached kernel_main =====")
        # Print register state at kernel_main
        gdb.execute("info registers")
        
        # Examine stack
        gdb.execute("x/16x $sp")
        
        # Check CurrentEL register
        gdb.execute("p/x $x0")  # First argument (UART address)
        gdb.execute("p/d (unsigned long)($CurrentEL >> 2) & 0x3")  # Exception level
        
        # Continue execution for a bit
        gdb.execute("next")
        gdb.execute("next")
        gdb.execute("next")
        
        # Check register values after a few instructions
        gdb.execute("info registers")
    else:
        print("\n===== Failed to reach kernel_main =====")
except:
    print("\n===== Failed to reach kernel_main =====")

print("\nDebugger session ready. Type 'c' to continue execution or use other GDB commands.") 