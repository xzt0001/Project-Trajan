.global user_test_svc
.type user_test_svc, %function

user_test_svc:
    // UART test - should appear immediately when task starts
    ldr x2, =0x09000000  // UART address
    mov w3, #'U'
    str w3, [x2]
    mov w3, #'A'
    str w3, [x2]
    mov w3, #'R'
    str w3, [x2]
    mov w3, #'T'
    str w3, [x2]
    mov w3, #':'
    str w3, [x2]
    mov w3, #'\n'
    str w3, [x2]

    // Print a banner message using UART directly
    adr x1, banner_message
    bl print_string_user

    // 1. Call sys_hello (SVC #0)
    adr x1, hello_message
    bl print_string_user
    svc #0

    // 2. Call sys_write with argument 0x1234 (SVC #1)
    adr x1, write_message
    bl print_string_user
    mov x0, #0x1234
    svc #1

    // 3. Call sys_yield (SVC #3)
    adr x1, yield_message
    bl print_string_user
    svc #3

    // 4. Call sys_exit with exit code 42 (SVC #2)
    adr x1, exit_message
    bl print_string_user
    mov x0, #42
    svc #2

    // If we return (which shouldn't happen), hang forever
    adr x1, hang_message
    bl print_string_user
hang:
    b hang

// Simple string printing function using direct UART access
// x1 = string address
print_string_user:
    ldr x2, =0x09000000  // UART address
1:
    ldrb w3, [x1], #1    // Load byte and increment
    cbz w3, 2f           // Exit if null byte
    str w3, [x2]         // Write to UART
    b 1b                 // Next character
2:
    ret

// Data section
.align 4
banner_message:
    .asciz "\n[USER] EL0 test program starting - Testing syscalls\n"

hello_message:
    .asciz "[USER] Calling sys_hello (SVC #0)\n"

write_message:
    .asciz "[USER] Calling sys_write (SVC #1) with arg=0x1234\n"

yield_message:
    .asciz "[USER] Calling sys_yield (SVC #3)\n"

exit_message:
    .asciz "[USER] Calling sys_exit (SVC #2) with exit_code=42\n"

hang_message:
    .asciz "[USER] Returned from syscalls (shouldn't happen)!\n"
