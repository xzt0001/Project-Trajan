.global user_test_svc
.type user_test_svc, %function

user_test_svc:
    mov x0, #42             // Arbitrary test value
    svc #0                  // Trigger syscall
    b .                     // Hang here after svc
