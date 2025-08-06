#include "../../../include/syscall.h"
#include "../../../include/uart.h"  // for uart_puts() and uart_hex64()

// Debug helper function to display a clear syscall boundary
void syscall_debug_marker(void) {
    uart_puts("\n====================\n");
    uart_puts("SYSCALL DEBUG MARKER\n");
    uart_puts("====================\n");
}

void sys_hello(void) {
    syscall_debug_marker();
    uart_puts("[SYSCALL] Hello from user task!\n");
}

void sys_write(uint64_t arg0) {
    syscall_debug_marker();
    uart_puts("[SYSCALL] Write called with arg: ");
    uart_hex64(arg0);
    uart_puts("\n");
}

void sys_exit(uint64_t exit_code) {
    syscall_debug_marker();
    uart_puts("[SYSCALL] Exit called with code: ");
    uart_hex64(exit_code);
    uart_puts("\n");
    // In a real implementation, this would terminate the current process
    // For now, just print the exit code
}

void sys_yield(void) {
    syscall_debug_marker();
    uart_puts("[SYSCALL] Yield called - would switch to next task\n");
    // In a real implementation, this would trigger a context switch
    // For now, just print a message
}

void syscall_dispatch(uint64_t num, struct trap_frame* tf) {
    // Add a clear marker to show the syscall dispatch is being called
    uart_puts("\n[SYSCALL DISPATCH] Received syscall #");
    uart_hex64(num);
    uart_puts("\n");
    
    switch (num) {
        case SYS_HELLO:
            uart_puts("[SYSCALL] Dispatching SYS_HELLO\n");
            sys_hello();
            break;
        case SYS_WRITE:
            uart_puts("[SYSCALL] Dispatching SYS_WRITE\n");
            sys_write(tf ? tf->x0 : 0);
            break;
        case SYS_EXIT:
            uart_puts("[SYSCALL] Dispatching SYS_EXIT\n");
            sys_exit(tf ? tf->x0 : 0);
            break;
        case SYS_YIELD:
            uart_puts("[SYSCALL] Dispatching SYS_YIELD\n");
            sys_yield();
            break;
        default:
            uart_puts("[SYSCALL] Unknown syscall number: ");
            uart_hex64(num);
            uart_puts("\n");
            break;
    }
}
