/*
 * arch_ops.h - Architecture-specific operations API
 * 
 * Provides a unified interface for architecture-specific operations including
 * VBAR management, vector table operations, and MMU transitions. These operations
 * are critical for proper system initialization and exception handling.
 */

#ifndef KERNEL_INIT_ARCH_OPS_H
#define KERNEL_INIT_ARCH_OPS_H

#include "../../../include/types.h"

/* ========== VBAR (Vector Base Address Register) Operations ========== */

/**
 * verify_vbar_el1 - Verify VBAR_EL1 is correctly set
 * 
 * Reads the current VBAR_EL1 value and compares it against the expected
 * vector table address. Performs alignment checks and memory accessibility
 * verification. Essential for diagnosing exception handler issues.
 */
void verify_vbar_el1(void);

/**
 * ensure_vbar_el1 - Check and restore VBAR_EL1 if needed
 * 
 * Defensive function that verifies VBAR_EL1 hasn't been corrupted
 * and restores it to the correct value if necessary. Can be called
 * periodically to detect and recover from VBAR corruption.
 */
void ensure_vbar_el1(void);

/**
 * write_vbar_el1 - Set VBAR_EL1 with verification
 * @address: Physical or virtual address to set as vector base
 * 
 * Sets VBAR_EL1 to the specified address and verifies the operation
 * succeeded. Provides debug output for visibility during boot.
 * Used for both physical (pre-MMU) and virtual (post-MMU) addresses.
 */
void write_vbar_el1(uint64_t address);

/**
 * update_vbar_to_virtual - OPTION D: Transition VBAR_EL1 to virtual address
 * 
 * Transitions VBAR_EL1 from physical identity mapping to high virtual address.
 * Must be called AFTER MMU is enabled and stable. This completes the 
 * physical→virtual transition for exception handling (Option D implementation).
 */
void update_vbar_to_virtual(void);

/**
 * init_traps - Initialize trap handlers with virtual addressing
 * 
 * Sets up VBAR_EL1 for virtual memory mode, using the mapped vector
 * table address. Called after MMU initialization to transition from
 * physical to virtual exception handling.
 */
void init_traps(void);

/**
 * init_exceptions_minimal - Minimal exception initialization
 * 
 * Simplified exception setup for early boot. Sets VBAR_EL1 with
 * minimal code and hangs on any errors. Used when full debug
 * infrastructure isn't available yet.
 */
void init_exceptions_minimal(void);

/* ========== Vector Table Operations ========== */

/**
 * validate_vector_table_at_0x89000 - Validate vector table at physical address
 * 
 * Checks if the vector table at physical address 0x89000 contains valid
 * ARM64 branch instructions. Used to verify vector table copy operations
 * and ensure the table is ready for use.
 */
void validate_vector_table_at_0x89000(void);

/**
 * copy_vector_table_to_ram_if_needed - Copy vector table to physical RAM
 * 
 * Copies the vector table from its load address to the target physical
 * address (0x89000) if needed. Performs validation and cache maintenance
 * to ensure the copy is successful and visible to the CPU.
 */
void copy_vector_table_to_ram_if_needed(void);

/**
 * verify_and_fix_vector_table - Comprehensive vector table verification
 * 
 * Performs extensive verification of the vector table including:
 * - Address display and alignment checks
 * - Memory accessibility verification  
 * - Exception vector validation
 * - Page table mapping and executable permissions
 * 
 * Critical for ensuring exception handlers will work correctly.
 */
void verify_and_fix_vector_table(void);

/**
 * verify_physical_vector_table - Verify vector table at physical 0x89000
 * 
 * Displays the contents of physical memory at 0x89000 to verify the
 * vector table was copied correctly. Shows both raw bytes and interprets
 * the first word as an ARM64 instruction.
 */
void verify_physical_vector_table(void);

/* ========== Future: MMU Transition Operations ========== */

/*
 * TODO: Add MMU transition functions:
 * - mmu_transition_enable() - Enable MMU and switch to virtual addressing
 * - mmu_rebind_console() - Update console to use virtual UART address
 * - mmu_verify_transition() - Verify MMU transition was successful
 * - cache_maintenance_pre_mmu() - Cache operations before MMU enable
 * - cache_maintenance_post_mmu() - Cache operations after MMU enable
 */

/* ========== Platform Constants ========== */

// Vector table alignment requirement (ARM64 specification)
#define VECTOR_ADDR_ALIGNMENT 0x800  // 2KB alignment

// Common physical addresses (TODO: move to platform config)
#define VECTORS_PHYS_ADDR    0x89000    // Physical vector table location
#define VECTORS_VIRT_ADDR    0x1000000  // Virtual vector table location

#endif /* KERNEL_INIT_ARCH_OPS_H */
