#include <stdint.h>

// Export symbol
void minimal_test_c(void) __attribute__((used, externally_visible));

// Extremely simple C function
void minimal_test_c(void) {
    // Write 'C' to UART
    *((volatile uint32_t*)(uintptr_t)0x09000000) = 'C';
} 