#ifndef MMU_POLICY_H
#define MMU_POLICY_H

#include "types.h"
#include "memory_config.h"

/**
 * @file mmu_policy.h
 * @brief AArch64 MMU Policy Layer - Authoritative System Register Programming
 * 
 * This header defines the policy layer for AArch64 MMU configuration.
 * All MMU-related system register writes, attribute definitions, and 
 * barrier sequences are controlled through this interface.
 * 
 * RED LINES (Exclusive to mmu_policy.c):
 * - Any writes to: MAIR_EL1, TCR_EL1, TTBR0_EL1, TTBR1_EL1, SCTLR_EL1
 * - Any TLBI instructions and barrier sequencing tied to enable/retune
 * - Global attribute encodings and TCR bitfield policies
 */

/* ========================================================================
 * MMU POLICY API - System Register Programming
 * ======================================================================== */

/**
 * @brief Configure MAIR_EL1 with standard memory attribute encodings
 * 
 * Programs MAIR_EL1 with the standard attribute byte values:
 * - Index 0: Device nGnRnE (0x00)
 * - Index 1: Normal WBWA (0xFF) 
 * - Index 2: Normal NC (0x44)
 * - Index 3: Device nGnRE (0x04)
 */
void mmu_configure_mair(void);

/**
 * @brief Configure TCR_EL1 for bootstrap dual-table mode
 * @param va_bits Virtual address bit width (39 or 48)
 * 
 * Programs TCR_EL1 with BOTH TTBR0 and TTBR1 enabled for bootstrap phase:
 * - T0SZ/T1SZ based on va_bits parameter
 * - 4KB granule for both TTBR0 and TTBR1
 * - Inner shareable, WBWA cacheable
 * - EPD0=0 (ENABLE TTBR0 for identity mapping during boot)
 * - EPD1=0 (ENABLE TTBR1 for high virtual mapping)
 * - IPS=1 (40-bit physical address space)
 * 
 * CRITICAL: Use this BEFORE enabling MMU. After jumping to high VA,
 * switch to kernel-only mode using mmu_configure_tcr_kernel_only().
 */
void mmu_configure_tcr_bootstrap_dual(unsigned va_bits);

/**
 * @brief Configure TCR_EL1 for kernel-only operation
 * @param va_bits Virtual address bit width (39 or 48)
 * 
 * Programs TCR_EL1 with:
 * - T0SZ/T1SZ based on va_bits parameter
 * - 4KB granule for both TTBR0 and TTBR1
 * - Inner shareable, WBWA cacheable
 * - EPD0=1 (disable TTBR0 for kernel-only operation)
 * - IPS=1 (40-bit physical address space)
 * 
 * CRITICAL: Only use this AFTER jumping to high VA in TTBR1 space.
 * Before MMU enable, use mmu_configure_tcr_bootstrap_dual().
 */
void mmu_configure_tcr_kernel_only(unsigned va_bits);

/**
 * @brief Set TTBR0_EL1 and TTBR1_EL1 base addresses
 * @param ttbr0_base Physical address of TTBR0 L0 page table
 * @param ttbr1_base Physical address of TTBR1 L0 page table
 * 
 * Writes both TTBR registers with proper ISB synchronization.
 * Verifies 4KB alignment of both base addresses.
 */
void mmu_set_ttbr_bases(uint64_t ttbr0_base, uint64_t ttbr1_base);

/**
 * @brief Comprehensive TLB invalidation sequence
 * 
 * Performs conservative, step-by-step TLB invalidation:
 * - Local TLB invalidation (vmalle1)
 * - Proper barrier sequencing (dsb nsh, isb)
 * - Skips instruction cache invalidation (often problematic)
 */
void mmu_comprehensive_tlbi_sequence(void);

/**
 * @brief Comprehensive TLB invalidation with verbose debug output
 * 
 * Same as mmu_comprehensive_tlbi_sequence but with detailed debug markers.
 * Use for critical MMU operations where you need detailed feedback.
 */
void mmu_comprehensive_tlbi_sequence_verbose(void);

/**
 * @brief Comprehensive TLB invalidation without debug output
 * 
 * Same TLB operations as the verbose version but silent.
 * Use for bulk operations to avoid flooding the console.
 */
void mmu_comprehensive_tlbi_sequence_quiet(void);

/**
 * @brief Enable MMU translation (SCTLR_EL1.M=1)
 * 
 * Performs the final MMU enable step:
 * - Sets SCTLR_EL1.M=1 (preserve all other bits)
 * - Mandatory ISB after SCTLR_EL1 write
 */
void mmu_enable_translation(void);

/**
 * @brief Set EPD for bootstrap dual-table mode
 * 
 * Configure TCR_EL1 for bootstrap phase:
 * - EPD0=0 (enable TTBR0 walks) 
 * - EPD1=0 (enable TTBR1 walks)
 * Allows safe transition with both address spaces active.
 */
void mmu_policy_set_epd_bootstrap_dual(void);

/**
 * @brief Set EPD for runtime kernel-only mode
 * 
 * Configure TCR_EL1 for runtime phase:
 * - EPD0=1 (disable TTBR0 walks)
 * - EPD1=0 (enable TTBR1 walks) 
 * Standard kernel configuration after MMU transition.
 */
void mmu_policy_set_epd_runtime_kernel(void);

/**
 * @brief Apply complete MMU policy and enable translation
 * @param ttbr0_base Physical address of TTBR0 L0 page table
 * @param ttbr1_base Physical address of TTBR1 L0 page table
 * @return 0 on success, -1 on failure
 * 
 * Complete MMU policy application in correct order:
 * 1. Configure MAIR_EL1
 * 2. Configure TCR_EL1
 * 3. Set TTBR bases
 * 4. TLB invalidation sequence
 * 5. Enable translation
 */
int mmu_apply_policy_and_enable(uint64_t ttbr0_base, uint64_t ttbr1_base);

/* ========================================================================
 * BARRIER AND SYNCHRONIZATION HELPERS
 * ======================================================================== */

/**
 * @brief Pre-MMU enable barrier sequence
 * 
 * Standard barrier sequence before MMU enable:
 * - dsb sy (system-wide data synchronization)
 * - isb (instruction synchronization)
 */
void mmu_barrier_sequence_pre_enable(void);

/**
 * @brief Post-MMU enable barrier sequence
 * 
 * Standard barrier sequence after MMU enable:
 * - isb (mandatory after SCTLR_EL1 change)
 * - dsb sy (system-wide data synchronization) 
 * - isb (final instruction synchronization)
 */
void mmu_barrier_sequence_post_enable(void);

/* ========================================================================
 * DEBUG AND DIAGNOSTIC HELPERS
 * ======================================================================== */

/**
 * @brief Decode memory attribute index to human-readable string
 * @param attr_idx Memory attribute index (0-3)
 * @return String description of the attribute
 */
const char* mmu_decode_attr_index(uint64_t attr_idx);

/**
 * @brief Check if attribute index represents device memory
 * @param attr_idx Memory attribute index (0-3)
 * @return true if device memory, false if normal memory
 */
bool mmu_is_device_memory(uint64_t attr_idx);

#endif /* MMU_POLICY_H */

