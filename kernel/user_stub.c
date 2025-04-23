// Stub file to ensure user_test_svc is linked
void user_test_svc(void) __attribute__((weak, externally_visible));

// This function will be used if user_test_svc is not defined elsewhere
void __attribute__((weak)) user_test_svc(void) {
    // This is just a fallback implementation
    // The actual implementation is in kernel/user.S
    volatile unsigned int* uart = (volatile unsigned int*)0x09000000;
    *uart = '[';
    *uart = 'S';
    *uart = 'V';
    *uart = 'C';
    *uart = ']';
    
    // Infinite loop
    while(1) {}
} 