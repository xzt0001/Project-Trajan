/**
 * @file mmu_policy.c
 * @brief AArch64 MMU Policy Layer - Authoritative System Register Programming
 * 
 * This module contains the authoritative implementation of MMU policy for AArch64.
 * All MMU-related system register writes, attribute definitions, and barrier 
 * sequences are controlled through this module.
 * 
 * RED LINES (Exclusive to this module):
 * - Any writes to: MAIR_EL1, TCR_EL1, TTBR0_EL1, TTBR1_EL1, SCTLR_EL1
 * - Any TLBI instructions and barrier sequencing tied to enable/retune
 * - Global attribute encodings and TCR bitfield policies
 */

#include "../include/mmu_policy.h"
#include "../include/types.h"
#include "../include/memory_config.h"
#include "../include/uart.h"

/* ========================================================================
 * PRIVATE HELPER FUNCTIONS
 * ======================================================================== */

/**
 * @brief Direct UART output for policy layer debugging
 * @param c Character to output
 */
static inline void policy_uart_putc(char c) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = c;
}

/**
 * @brief Direct UART debug marker sequence output
 * @param markers Array of characters to output as debug markers
 * @param count Number of characters to output
 */
static void policy_uart_markers(const char* markers, int count) {
    for (int i = 0; i < count; i++) {
        policy_uart_putc(markers[i]);
    }
}

/* ========================================================================
 * MMU POLICY API IMPLEMENTATION
 * ======================================================================== */

void mmu_configure_mair(void) {
    // S1:MAIR:START
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'S'; *uart = '1'; *uart = ':'; *uart = 'M'; *uart = 'A'; *uart = 'I'; *uart = 'R'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Exception level verification before register write
    uint64_t current_el;
    __asm__ volatile("mrs %0, currentel" : "=r"(current_el));
    current_el = (current_el >> 2) & 0x3;
    *uart = 'M'; *uart = 'E'; *uart = 'L'; *uart = ':';
    *uart = '0' + current_el;
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Read current MAIR value for comparison
    uint64_t old_mair;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(old_mair));
    *uart = 'M'; *uart = 'O'; *uart = 'L'; *uart = 'D'; *uart = ':';
    uart_hex64_early(old_mair);
    *uart = '\r'; *uart = '\n';
    
    // Debug: Test constant values first
    *uart = 'M'; *uart = 'C'; *uart = 'H'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    volatile uint64_t test1 = MAIR_ATTR_DEVICE_nGnRnE;
    volatile uint64_t test2 = ATTR_IDX_DEVICE_nGnRnE;
    (void)test1; (void)test2; // Avoid unused warnings
    
    // Build MAIR_EL1 value using authoritative attribute encodings
    // Extracted from memory_core.c:260-263
    *uart = 'M'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    uint64_t mair = (MAIR_ATTR_DEVICE_nGnRnE << (8 * ATTR_IDX_DEVICE_nGnRnE)) |
                    (MAIR_ATTR_NORMAL << (8 * ATTR_IDX_NORMAL)) |
                    (MAIR_ATTR_NORMAL_NC << (8 * ATTR_IDX_NORMAL_NC)) |
                    (MAIR_ATTR_DEVICE_nGnRE << (8 * ATTR_IDX_DEVICE_nGnRE));
    
    *uart = 'M'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    *uart = 'M'; *uart = 'N'; *uart = 'E'; *uart = 'W'; *uart = ':';
    uart_hex64_early(mair);
    *uart = '\r'; *uart = '\n';
    
    // CRITICAL: Write MAIR_EL1 register with extra safety
    *uart = 'M'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    __asm__ volatile(
        "msr mair_el1, %0\n"
        "isb\n"
        :: "r"(mair) : "memory"
    );
    *uart = 'M'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Verify the write took effect
    uint64_t verify_mair;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(verify_mair));
    *uart = 'M'; *uart = 'V'; *uart = 'F'; *uart = 'Y'; *uart = ':';
    uart_hex64_early(verify_mair);
    *uart = '\r'; *uart = '\n';
    
    if (verify_mair == mair) {
        *uart = 'S'; *uart = '1'; *uart = ':'; *uart = 'M'; *uart = ':'; *uart = 'S'; *uart = 'U'; *uart = 'C'; *uart = 'C'; *uart = 'E'; *uart = 'S'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'S'; *uart = '1'; *uart = ':'; *uart = 'M'; *uart = ':'; *uart = 'M'; *uart = 'I'; *uart = 'S'; *uart = 'M'; *uart = 'A'; *uart = 'T'; *uart = 'C'; *uart = 'H';
        *uart = '\r'; *uart = '\n';
    }
}

/**
 * @brief Configure TCR_EL1 for bootstrap dual-table mode (EPD0=0, EPD1=0)
 * 
 * This function configures TCR_EL1 with BOTH TTBR0 and TTBR1 enabled for the
 * bootstrap phase where we need identity mapping (TTBR0) and high virtual 
 * mapping (TTBR1) active simultaneously.
 * 
 * CRITICAL: This must be used BEFORE enabling MMU via trampoline.
 * After jumping to high VA, switch to kernel-only mode (EPD0=1).
 */
void mmu_configure_tcr_bootstrap_dual(unsigned va_bits) {
    // S2:TCR:BOOT
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = ':'; *uart = 'B'; *uart = 'O'; *uart = 'O'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Exception level and current TCR value
    uint64_t current_el;
    __asm__ volatile("mrs %0, currentel" : "=r"(current_el));
    current_el = (current_el >> 2) & 0x3;
    *uart = 'T'; *uart = 'E'; *uart = 'L'; *uart = ':';
    *uart = '0' + current_el;
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Read current TCR value
    uint64_t old_tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(old_tcr));
    *uart = 'T'; *uart = 'O'; *uart = 'L'; *uart = 'D'; *uart = ':';
    uart_hex64_early(old_tcr);
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Show VA bits parameter
    *uart = 'T'; *uart = 'V'; *uart = 'A'; *uart = ':';
    *uart = '0' + (va_bits / 10);
    *uart = '0' + (va_bits % 10);
    *uart = '\r'; *uart = '\n';
    
    // Build TCR_EL1 value for BOOTSTRAP DUAL-TABLE MODE
    *uart = 'T'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    uint64_t tcr = 0;
    
    // VA size configuration based on va_bits parameter
    if (va_bits == 48) {
        tcr |= ((uint64_t)TCR_T0SZ_POLICY << 0);   // T0SZ = 16 for 48-bit
        tcr |= ((uint64_t)TCR_T1SZ_POLICY << 16);  // T1SZ = 16 for 48-bit
    } else {
        tcr |= ((uint64_t)25 << 0);                // T0SZ = 25 for 39-bit  
        tcr |= ((uint64_t)25 << 16);               // T1SZ = 25 for 39-bit
    }
    
    // TG0[15:14] = 0 (4KB granule for TTBR0_EL1)
    tcr |= (0ULL << 14);
    
    // TG1[31:30] = 0 (4KB granule for TTBR1_EL1) 
    tcr |= (0ULL << 30);
    
    // SH0[13:12] = 3 (Inner shareable for TTBR0)
    tcr |= (3ULL << 12);
    
    // SH1[29:28] = 3 (Inner shareable for TTBR1)
    tcr |= (3ULL << 28);
    
    // ORGN0[11:10] = 1 (Outer Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 10);
    
    // ORGN1[27:26] = 1 (Outer Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 26);
    
    // IRGN0[9:8] = 1 (Inner Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 8);
    
    // IRGN1[25:24] = 1 (Inner Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 24);
    
    // ✅ CRITICAL DIFFERENCE: EPD0 = 0 (Enable TTBR0 for bootstrap phase)
    tcr |= (0ULL << 7);  // ← CHANGED FROM 1 TO 0 (vs kernel_only)
    
    // EPD1 = 0 (Enable TTBR1 page table walks for kernel)  
    tcr |= (0ULL << 23);
    
    // IPS[34:32] = 1 (40-bit physical address size)
    tcr |= (1ULL << 32);
    
    // TBI0 = 1 (Top Byte Ignored for TTBR0)
    tcr |= (1ULL << 37);
    
    // TBI1 = 1 (Top Byte Ignored for TTBR1)
    tcr |= (1ULL << 38);
    
    // AS = 0 (ASID size is 8-bit)
    tcr |= (0ULL << 36);
    
    *uart = 'T'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    *uart = 'T'; *uart = 'N'; *uart = 'E'; *uart = 'W'; *uart = ':';
    uart_hex64_early(tcr);
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Show EPD0 value explicitly
    *uart = 'E'; *uart = 'P'; *uart = 'D'; *uart = '0'; *uart = ':';
    *uart = '0' + ((tcr >> 7) & 1);  // Should print '0'
    *uart = '\r'; *uart = '\n';
    
    // CRITICAL: Write TCR_EL1 register with safety barriers
    *uart = 'T'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    __asm__ volatile(
        "msr tcr_el1, %0\n"
        "isb\n"
        :: "r"(tcr) : "memory"
    );
    *uart = 'T'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // DEBUG: Verify the write took effect
    uint64_t verify_tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(verify_tcr));
    *uart = 'T'; *uart = 'V'; *uart = 'F'; *uart = 'Y'; *uart = ':';
    uart_hex64_early(verify_tcr);
    *uart = '\r'; *uart = '\n';
    
    if (verify_tcr == tcr) {
        *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'U'; *uart = 'C'; *uart = 'C'; *uart = 'E'; *uart = 'S'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'M'; *uart = 'I'; *uart = 'S'; *uart = 'M'; *uart = 'A'; *uart = 'T'; *uart = 'C'; *uart = 'H';
        *uart = '\r'; *uart = '\n';
    }
}

void mmu_configure_tcr_kernel_only(unsigned va_bits) {
    // S2:TCR:START
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = 'C'; *uart = 'R'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Exception level and current TCR value
    uint64_t current_el;
    __asm__ volatile("mrs %0, currentel" : "=r"(current_el));
    current_el = (current_el >> 2) & 0x3;
    *uart = 'T'; *uart = 'E'; *uart = 'L'; *uart = ':';
    *uart = '0' + current_el;
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Read current TCR value
    uint64_t old_tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(old_tcr));
    *uart = 'T'; *uart = 'O'; *uart = 'L'; *uart = 'D'; *uart = ':';
    uart_hex64_early(old_tcr);
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Show VA bits parameter
    *uart = 'T'; *uart = 'V'; *uart = 'A'; *uart = ':';
    *uart = '0' + (va_bits / 10);
    *uart = '0' + (va_bits % 10);
    *uart = '\r'; *uart = '\n';
    
    // Build TCR_EL1 value 
    // Extracted from memory_core.c:191-240
    *uart = 'T'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    uint64_t tcr = 0;
    
    // VA size configuration based on va_bits parameter
    if (va_bits == 48) {
        tcr |= ((uint64_t)TCR_T0SZ_POLICY << 0);   // T0SZ = 16 for 48-bit
        tcr |= ((uint64_t)TCR_T1SZ_POLICY << 16);  // T1SZ = 16 for 48-bit
    } else {
        tcr |= ((uint64_t)25 << 0);                // T0SZ = 25 for 39-bit  
        tcr |= ((uint64_t)25 << 16);               // T1SZ = 25 for 39-bit
    }
    
    // TG0[15:14] = 0 (4KB granule for TTBR0_EL1)
    tcr |= (0ULL << 14);
    
    // TG1[31:30] = 0 (4KB granule for TTBR1_EL1) 
    tcr |= (0ULL << 30);
    
    // SH0[13:12] = 3 (Inner shareable for TTBR0)
    tcr |= (3ULL << 12);
    
    // SH1[29:28] = 3 (Inner shareable for TTBR1)
    tcr |= (3ULL << 28);
    
    // ORGN0[11:10] = 1 (Outer Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 10);
    
    // ORGN1[27:26] = 1 (Outer Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 26);
    
    // IRGN0[9:8] = 1 (Inner Write-Back, Read/Write Allocate for TTBR0)
    tcr |= (1ULL << 8);
    
    // IRGN1[25:24] = 1 (Inner Write-Back, Read/Write Allocate for TTBR1)
    tcr |= (1ULL << 24);
    
    // EPD0 = 1 (Disable TTBR0 page table walks initially - kernel only)
    tcr |= (1ULL << 7);
    
    // EPD1 = 0 (Enable TTBR1 page table walks for kernel)  
    tcr |= (0ULL << 23);
    
    // IPS[34:32] = 1 (40-bit physical address size)
    tcr |= (1ULL << 32);
    
    // TBI0 = 1 (Top Byte Ignored for TTBR0)
    tcr |= (1ULL << 37);
    
    // TBI1 = 1 (Top Byte Ignored for TTBR1)
    tcr |= (1ULL << 38);
    
    // AS = 0 (ASID size is 8-bit)
    tcr |= (0ULL << 36);
    
    *uart = 'T'; *uart = 'B'; *uart = 'L'; *uart = 'D'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    *uart = 'T'; *uart = 'N'; *uart = 'E'; *uart = 'W'; *uart = ':';
    uart_hex64_early(tcr);
    *uart = '\r'; *uart = '\n';
    
    // CRITICAL: Write TCR_EL1 register with safety barriers
    *uart = 'T'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    __asm__ volatile(
        "msr tcr_el1, %0\n"
        "isb\n"
        :: "r"(tcr) : "memory"
    );
    *uart = 'T'; *uart = 'W'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // RESTORED DEBUG: Verify the write took effect
    uint64_t verify_tcr;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(verify_tcr));
    *uart = 'T'; *uart = 'V'; *uart = 'F'; *uart = 'Y'; *uart = ':';
    uart_hex64_early(verify_tcr);
    *uart = '\r'; *uart = '\n';
    
    if (verify_tcr == tcr) {
        *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'U'; *uart = 'C'; *uart = 'C'; *uart = 'E'; *uart = 'S'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'S'; *uart = '2'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'M'; *uart = 'I'; *uart = 'S'; *uart = 'M'; *uart = 'A'; *uart = 'T'; *uart = 'C'; *uart = 'H';
        *uart = '\r'; *uart = '\n';
    }
}

void mmu_set_ttbr_bases(uint64_t ttbr0_base, uint64_t ttbr1_base) {
    // S3:TTBR:START
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'S'; *uart = '3'; *uart = ':'; *uart = 'T'; *uart = 'T'; *uart = 'B'; *uart = 'R'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Verify alignment (must be 4KB aligned)
    if ((ttbr0_base & 0xFFF) || (ttbr1_base & 0xFFF)) {
        *uart = 'E'; *uart = 'R'; *uart = 'R'; *uart = 'O'; *uart = 'R'; *uart = ':'; *uart = 'A'; *uart = 'L'; *uart = 'I'; *uart = 'G'; *uart = 'N';
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Write TTBR registers
    // Extracted from memory_core.c:391-393
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"       // Set TTBR0_EL1
        "msr ttbr1_el1, %1\n"       // Set TTBR1_EL1  
        "isb\n"                     // Mandatory synchronization
        :: "r"(ttbr0_base), "r"(ttbr1_base)
    );
    
    // RESTORED DEBUG: Verify the writes took effect
    uint64_t verify_ttbr0, verify_ttbr1;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(verify_ttbr0));
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(verify_ttbr1));
    *uart = 'T'; *uart = '0'; *uart = 'V'; *uart = 'F'; *uart = 'Y'; *uart = ':';
    uart_hex64_early(verify_ttbr0);
    *uart = '\r'; *uart = '\n';
    *uart = 'T'; *uart = '1'; *uart = 'V'; *uart = 'F'; *uart = 'Y'; *uart = ':';
    uart_hex64_early(verify_ttbr1);
    *uart = '\r'; *uart = '\n';
    
    if (verify_ttbr0 == ttbr0_base && verify_ttbr1 == ttbr1_base) {
        *uart = 'S'; *uart = '3'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'U'; *uart = 'C'; *uart = 'C'; *uart = 'E'; *uart = 'S'; *uart = 'S';
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'S'; *uart = '3'; *uart = ':'; *uart = 'T'; *uart = ':'; *uart = 'M'; *uart = 'I'; *uart = 'S'; *uart = 'M'; *uart = 'A'; *uart = 'T'; *uart = 'C'; *uart = 'H';
        *uart = '\r'; *uart = '\n';
    }
}

void mmu_comprehensive_tlbi_sequence(void) {
    // Call the verbose version for backward compatibility
    mmu_comprehensive_tlbi_sequence_verbose();
}

void mmu_comprehensive_tlbi_sequence_verbose(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Compact single-line format: TLB:12345OK (12 chars vs 7 lines!)
    *uart = 'T'; *uart = 'L'; *uart = 'B'; *uart = ':';
    
    // Step 1: Basic data synchronization
    __asm__ volatile("dsb sy" ::: "memory");  // System-wide
    *uart = '1';
    
    // Step 2: Local TLB invalidation (no inner-shareable domain)
    __asm__ volatile("tlbi vmalle1" ::: "memory");  // Local core only
    *uart = '2';
    
    // Step 3: Wait for TLB operation completion
    __asm__ volatile("dsb nsh" ::: "memory");  // Non-shareable domain only
    *uart = '3';
    
    // Step 4: Skip instruction cache invalidation (often problematic)
    *uart = '4';
    
    // Step 5: Final barrier
    __asm__ volatile("isb" ::: "memory");
    *uart = '5';
    
    // Completion marker
    *uart = 'O'; *uart = 'K';
    // No newline - keeps output compact on same line
}

void mmu_comprehensive_tlbi_sequence_quiet(void) {
    // Same TLB operations but no debug output
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb nsh" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

void mmu_enable_translation(void) {
    // MMU:ENABLE
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'E'; *uart = 'N'; *uart = 'A'; *uart = 'B'; *uart = 'L'; *uart = 'E';
    *uart = '\r'; *uart = '\n';
    
    // Read current SCTLR_EL1 to preserve RES1 bits
    uint64_t sctlr_val;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    
    // Set MMU enable bit (M=1) while preserving all other bits
    sctlr_val |= 0x1;
    
    // Critical: Single MMU enable attempt
    // Extracted from memory_core.c:1297-1298
    __asm__ volatile(
        "msr sctlr_el1, %0\n"       // Write SCTLR with M=1
        "isb\n"                     // Mandatory instruction synchronization
        :: "r"(sctlr_val)
    );
    
    // Verify MMU is enabled
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    if (sctlr_val & 0x1) {
        *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'O'; *uart = 'K';
        *uart = '\r'; *uart = '\n';
    } else {
        *uart = 'M'; *uart = 'M'; *uart = 'U'; *uart = ':'; *uart = 'F'; *uart = 'A'; *uart = 'I'; *uart = 'L';
        *uart = '\r'; *uart = '\n';
    }
}

void mmu_policy_set_epd_bootstrap_dual(void) {
    // Keep all existing settings, only touch EPD bits.
    uint64_t tcr; __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    tcr &= ~((1ULL<<7) | (1ULL<<23)); // clear EPD0, EPD1
    // EPD0=0, EPD1=0  (both walks enabled)
    __asm__ volatile("msr tcr_el1, %0\nisb" :: "r"(tcr) : "memory");
}

void mmu_policy_set_epd_runtime_kernel(void) {
    uint64_t tcr; __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    tcr |=  (1ULL<<7);                // EPD0=1 (disable TTBR0 walks)
    tcr &= ~(1ULL<<23);               // EPD1=0 (enable TTBR1 walks)
    __asm__ volatile("msr tcr_el1, %0\nisb" :: "r"(tcr) : "memory");
}

int mmu_apply_policy_and_enable(uint64_t ttbr0_base, uint64_t ttbr1_base) {
    // POLICY:START
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'P'; *uart = 'O'; *uart = 'L'; *uart = 'I'; *uart = 'C'; *uart = 'Y'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Step 1: Configure memory attributes
    mmu_configure_mair();
    
    // Step 2: Configure translation control  
    mmu_configure_tcr_kernel_only(VA_BITS_48 ? 48 : 39);
    
    // Step 3: Set translation table bases
    mmu_set_ttbr_bases(ttbr0_base, ttbr1_base);
    
    // Step 4: Pre-enable synchronization
    mmu_barrier_sequence_pre_enable();
    mmu_comprehensive_tlbi_sequence();
    
    // Step 5: Enable translation
    mmu_enable_translation();
    
    // Step 6: Post-enable synchronization  
    mmu_barrier_sequence_post_enable();
    
    *uart = 'P'; *uart = 'O'; *uart = 'L'; *uart = 'I'; *uart = 'C'; *uart = 'Y'; *uart = ':'; *uart = 'C'; *uart = 'O'; *uart = 'M'; *uart = 'P'; *uart = 'L'; *uart = 'E'; *uart = 'T'; *uart = 'E';
    *uart = '\r'; *uart = '\n';
    return 0; // Success
}

/* ========================================================================
 * BARRIER AND SYNCHRONIZATION HELPERS
 * ======================================================================== */

void mmu_barrier_sequence_pre_enable(void) {
    // BARRIER:PRE
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':'; *uart = 'P'; *uart = 'R'; *uart = 'E';
    *uart = '\r'; *uart = '\n';
    
    // Standard barrier sequence before MMU enable
    __asm__ volatile("dsb sy" ::: "memory");  // System-wide data synchronization
    __asm__ volatile("isb" ::: "memory");     // Instruction synchronization
    
    *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':'; *uart = 'P'; *uart = 'R'; *uart = 'E'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
}

void mmu_barrier_sequence_post_enable(void) {
    // BARRIER:POST
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':'; *uart = 'P'; *uart = 'O'; *uart = 'S'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Standard barrier sequence after MMU enable
    __asm__ volatile("isb" ::: "memory");     // Mandatory after SCTLR_EL1 change
    __asm__ volatile("dsb sy" ::: "memory");  // System-wide data synchronization
    __asm__ volatile("isb" ::: "memory");     // Final instruction synchronization
    
    *uart = 'B'; *uart = 'A'; *uart = 'R'; *uart = ':'; *uart = 'P'; *uart = 'O'; *uart = 'S'; *uart = 'T'; *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
}

/* ========================================================================
 * DEBUG AND DIAGNOSTIC HELPERS
 * ======================================================================== */

const char* mmu_decode_attr_index(uint64_t attr_idx) {
    switch (attr_idx) {
        case ATTR_IDX_DEVICE_nGnRnE: return "Device nGnRnE";
        case ATTR_IDX_NORMAL:        return "Normal WBWA";
        case ATTR_IDX_NORMAL_NC:     return "Normal NC";
        case ATTR_IDX_DEVICE_nGnRE:  return "Device nGnRE";
        default:                     return "Unknown";
    }
}

bool mmu_is_device_memory(uint64_t attr_idx) {
    return (attr_idx == ATTR_IDX_DEVICE_nGnRnE) || 
           (attr_idx == ATTR_IDX_DEVICE_nGnRE);
}
