# GDB script for debugging kernel
set architecture aarch64
set confirm off
set pagination off

# Connect to QEMU's GDB server
target remote localhost:1234

# Set a breakpoint at _start
break _start
commands
  # Print information about execution context
  print "At _start"
  info registers
end

# Set breakpoint at kernel_main
break kernel_main
commands
  # Print information about execution context
  print "At kernel_main"
  info registers
  x/10i $pc-8  # Examine instructions around current PC
  
  # Check CurrentEL register
  monitor system_register read currentel
end

# Initial commands to run
echo Waiting for QEMU connection...\n
continue

# Show PC and disassemble _start
disas _start
x/10i $pc

# Now continue to kernel_main or until timeout
echo Continuing to kernel_main...\n
continue

# If we get here, either we've hit the breakpoint at kernel_main
# or execution has stopped somewhere else
where
info registers 