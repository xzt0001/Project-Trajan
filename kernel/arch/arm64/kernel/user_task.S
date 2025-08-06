// Simple EL0 user task for testing SVC calls
.global user_task
.type user_task, %function

.align 4  // Align to 16 bytes
user_task:
    // Simple message to confirm we're in the user task
    adr x1, user_message
    bl print_string_user

    // Set arbitrary test value in x0
    mov x0, #42         // Arbitrary test value
    
    // Trigger system call
    svc #0              // Execute supervisor call
    
    // If we return (we shouldn't), loop forever
    adr x1, svc_return_message
    bl print_string_user
    b .                 // Infinite loop

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
    ret                  // Return

// Data section
.align 4
user_message:
    .asciz "[USER] EL0 task running, calling SVC #0...\n"
    
svc_return_message:
    .asciz "[USER] Returned from SVC (shouldn't happen)!\n" 