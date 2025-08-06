#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

/**
 * Debug Configuration for CustomOS Kernel
 * 
 * This header controls the verbosity of debug output during boot.
 * Different build configurations can enable/disable various debug levels.
 */

// ============================================================================
// DEBUG LEVELS - Choose one
// ============================================================================

// Uncomment ONE of these based on your needs:

// 1. FULL VERBOSE DEBUG (Development)
#define DEBUG_BOOT_VERBOSE      // Complete debug output with test patterns

// 2. MODERATE DEBUG (Testing)
// #define DEBUG_BOOT_MODERATE   // Key markers only, no test patterns

// 3. MINIMAL DEBUG (Production)
// #define DEBUG_BOOT_MINIMAL    // Essential markers only

// 4. SILENT (Release)
// #define DEBUG_BOOT_SILENT     // No debug output

// ============================================================================
// DEBUG FEATURE FLAGS (Automatically set based on level)
// ============================================================================

#ifdef DEBUG_BOOT_VERBOSE
    #define DEBUG_TEST_PATTERNS_ENABLED    // 0xcafebabedeadbeef test patterns
    #define DEBUG_MEMORY_ADDRESSES_ENABLED // Detailed memory address output
    #define DEBUG_MARKERS_CD_ENABLED       // 'CD' core debug markers  
    #define DEBUG_MARKERS_KMV_ENABLED      // 'K', 'M', 'V' markers
    #define DEBUG_MARKERS_EXTRA_ENABLED    // Additional debug markers
    #define DEBUG_TIMING_DELAYS_ENABLED    // UART delays for readability
#endif

#ifdef DEBUG_BOOT_MODERATE
    #define DEBUG_MEMORY_ADDRESSES_ENABLED
    #define DEBUG_MARKERS_CD_ENABLED
    #define DEBUG_MARKERS_KMV_ENABLED
    #define DEBUG_TIMING_DELAYS_ENABLED
    // No test patterns
#endif

#ifdef DEBUG_BOOT_MINIMAL
    #define DEBUG_MARKERS_CD_ENABLED
    // Only essential markers
#endif

#ifdef DEBUG_BOOT_SILENT
    // No debug features enabled
#endif

// ============================================================================
// DEBUG MACROS
// ============================================================================

#ifdef DEBUG_TEST_PATTERNS_ENABLED
    #define DEBUG_OUTPUT_TEST_PATTERNS() do { \
        uart_hex64_early(0xCAFEBABEDEADBEEF); \
        uart_hex64_early(0xCAFEBABEDEADBEEF); \
        uart_hex64_early(0x0123456789ABCDEF); \
        uart_hex64_early(0x0123456789ABCDEF); \
        uart_hex64_early(0xFEDCBA9876543210); \
        uart_hex64_early(0xFEDCBA9876543210); \
    } while(0)
#else
    #define DEBUG_OUTPUT_TEST_PATTERNS() do { } while(0)
#endif

#ifdef DEBUG_MARKERS_CD_ENABLED
    #define DEBUG_MARKER_C() uart_putc_early('C')
    #define DEBUG_MARKER_D() uart_putc_early('D')
#else
    #define DEBUG_MARKER_C() do { } while(0)
    #define DEBUG_MARKER_D() do { } while(0)
#endif

#ifdef DEBUG_MARKERS_KMV_ENABLED
    #define DEBUG_MARKER_K() uart_putc_early('K')
    #define DEBUG_MARKER_M() uart_putc_early('M')
    #define DEBUG_MARKER_V() uart_putc_early('V')
#else
    #define DEBUG_MARKER_K() do { } while(0)
    #define DEBUG_MARKER_M() do { } while(0)
    #define DEBUG_MARKER_V() do { } while(0)
#endif

#ifdef DEBUG_TIMING_DELAYS_ENABLED
    #define DEBUG_UART_DELAY() uart_delay_short()
#else
    #define DEBUG_UART_DELAY() do { } while(0)
#endif

// ============================================================================
// CONFIGURATION INFO
// ============================================================================

#ifdef DEBUG_BOOT_VERBOSE
    #define DEBUG_CONFIG_NAME "VERBOSE"
    #define DEBUG_CONFIG_DESC "Full debug output with test patterns"
#elif defined(DEBUG_BOOT_MODERATE)
    #define DEBUG_CONFIG_NAME "MODERATE" 
    #define DEBUG_CONFIG_DESC "Key markers, no test patterns"
#elif defined(DEBUG_BOOT_MINIMAL)
    #define DEBUG_CONFIG_NAME "MINIMAL"
    #define DEBUG_CONFIG_DESC "Essential markers only"
#elif defined(DEBUG_BOOT_SILENT)
    #define DEBUG_CONFIG_NAME "SILENT"
    #define DEBUG_CONFIG_DESC "No debug output"
#else
    #define DEBUG_CONFIG_NAME "UNDEFINED"
    #define DEBUG_CONFIG_DESC "No debug level selected"
    #error "Please define a debug level: DEBUG_BOOT_VERBOSE, DEBUG_BOOT_MODERATE, DEBUG_BOOT_MINIMAL, or DEBUG_BOOT_SILENT"
#endif

#endif // DEBUG_CONFIG_H