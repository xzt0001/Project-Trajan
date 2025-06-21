#ifndef UART_H
#define UART_H

#include "types.h"

/*
 * ---------------- Virtual-address layout configuration ----------------
 *
 * This project can run either in a 39-bit or a 48-bit VA space.  The
 * selection is controlled at compile time with VA_BITS_48 (default = 1).
 *   VA_BITS_48 = 0  →  39-bit layout (T0SZ/T1SZ = 25)
 *   VA_BITS_48 = 1  →  48-bit layout (T0SZ/T1SZ = 16)
 *
 * HIGH_VIRT_BASE and related constants are derived accordingly.  If you
 * want to test on older cores that do *not* support 48-bit VAs, simply
 * re-build with
 *      #define VA_BITS_48 0
 * before including any headers (for example via compiler flag
 * -DVA_BITS_48=0).
 */

#ifndef VA_BITS_48
#define VA_BITS_48 1   /* Default: use the 48-bit scheme */
#endif

#if VA_BITS_48
#define HIGH_VIRT_BASE 0xFFFF800000000000UL /* canonical for 48-bit */
#define TCR_T0SZ 16
#define TCR_T1SZ 16
#else
#define HIGH_VIRT_BASE 0xFFFFFF8000000000UL /* canonical for 39-bit */
#define TCR_T0SZ 25
#define TCR_T1SZ 25
#endif

// UART physical and virtual addresses (kept for reference)
#define UART_PHYS 0x09000000
#define UART_VIRT (HIGH_VIRT_BASE + 0x09000000UL)

// Global flag to track MMU status
extern bool mmu_enabled;

// Global string buffers for MMU transition safety
extern volatile char global_string_buffer[];
extern volatile char global_temp_buffer[];

// Global UART base pointer - used by all UART functions
extern volatile uint32_t* g_uart_base;

// Access function for the static mmu_msg buffer
volatile char* get_mmu_msg_buffer(void);

// Debug mode for UART - controls extra debug output
#define DEBUG_UART_MODE 1

// Initialize UART (pre-MMU) - maps to uart_init_early in uart_early.c
void uart_init_early(unsigned long uart_addr);

// Pre-MMU UART functions - located in uart_early.c
void uart_putc_early(char c);
void uart_puts_early(const char* str);
void uart_hex64_early(uint64_t value);
void uart_debug_marker(char marker);
void uart_clear_screen(void);

// Post-MMU UART functions - located in uart_late.c
void uart_putc_late(char c);
void uart_puts_late(const char* str);
void uart_hex64_late(uint64_t value);
void uart_debug_marker_late(char marker);
void uart_debug_hex(uint64_t val);
void uart_test_virt_mapping(void);

// Safe string handling functions for MMU transition
// These contain additional cache maintenance and safety checks
void uart_puts_safe_indexed(const char* str); // Character-by-character access with cache maintenance
bool uart_puts_with_fallback(const char* str); // Tries multiple methods to ensure output

// Emergency direct UART functions - maximum reliability
void uart_emergency_output(char c);
void uart_emergency_puts(const char* str);
void uart_emergency_hex64(uint64_t value);

// UART mapping verification and debugging functions
void verify_uart_mapping(void); // Verify UART virtual mapping after MMU is enabled
uint64_t read_pte_entry(uint64_t va); // Read page table entry for a virtual address

// Legacy functions that will be replaced with _early or _late variants
// These are kept for backward compatibility during transition
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* str);
void uart_puthex(uint64_t value);
void uart_print_hex(uint64_t value);
void uart_hex64(uint64_t value);
void uart_putx(uint64_t value);
void uart_putc_raw(char c);
void uart_panic(const char* str);

// UART base address update function - called during MMU transition
void uart_set_base(void* addr);

// Set MMU status flag with barriers
void uart_set_mmu_enabled(void);

#endif /* UART_H */
