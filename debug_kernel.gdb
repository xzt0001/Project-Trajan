# Basic GDB script for debugging the kernel
set confirm off
set pagination off
set architecture aarch64

# Connect to QEMU's GDB server
target remote localhost:1234

# Display a message
echo \nConnecting to QEMU on localhost:1234...\n

# Load symbols from the ELF file if not already loaded
file build/kernel.elf

# Set a breakpoint at _start and kernel_main
break _start
break kernel_main

# Continue execution until we hit _start
continue

# Print registers and stack when hitting breakpoints
define hook-stop
  echo \n
  info registers
  x/5i $pc
  echo \n=== STACK ===\n
  x/8xg $sp
  echo \n
end

# Command to examine CurrentEL register
define current_el
  set $el = 0
  monitor info registers | grep ELR_EL
  printf "CurrentEL value: %d\n", ($el >> 2) & 0x3
end

# Command to manually step with register display
define nstep
  stepi
  info registers
  x/5i $pc
end

echo \nGDB initialized. Type 'continue' or 'c' to start execution.\n
echo Use 'current_el' to check exception level.\n
echo Use 'nstep' to step with register display.\n 