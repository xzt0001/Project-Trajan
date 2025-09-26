#ifndef MEMORY_CONFIG_H
#define MEMORY_CONFIG_H

#include "types.h"
#include "uart.h"

/**
 * @file memory_config.h
 * @brief Memory Management Configuration Constants and Type Definitions
 * 
 * This header file contains configuration constants, type definitions, and 
 * global variable declarations for the virtual memory management system.
 * 
 * The constants and types defined here are used throughout the kernel for
 * page table management, memory mapping, and MMU configuration.
 */

/* ========================================================================
 * PAGE TABLE CONFIGURATION
 * ======================================================================== */

/** Page size and alignment constants */
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1 << PAGE_SHIFT)  // 4096 bytes
#define ENTRIES_PER_TABLE 512

/* ========================================================================
 * ARMV8 PAGE TABLE ENTRY FLAGS
 * ======================================================================== */

/** Basic page table entry flags */
#ifndef PTE_VALID
#define PTE_VALID       (1UL << 0)   // Entry is valid
#endif

#ifndef PTE_TABLE  
#define PTE_TABLE       (1UL << 1)   // Entry points to another table
#endif

#define PTE_AF          (1UL << 10)  // Access Flag - must be set to avoid access faults

/* ========================================================================
 * MMU POLICY CONSTANTS (Authoritative - Do Not Duplicate)
 * ======================================================================== */

/** Translation Control Register Configuration */
#ifndef VA_BITS_48
#define VA_BITS_48 1   /* Default: use the 48-bit scheme */
#endif

#if VA_BITS_48
#define TCR_T0SZ_POLICY 16  // 48-bit VA space
#define TCR_T1SZ_POLICY 16
#define HIGH_VIRT_BASE_POLICY 0xFFFF800000000000UL /* canonical for 48-bit */
#else
#define TCR_T0SZ_POLICY 25  // 39-bit VA space  
#define TCR_T1SZ_POLICY 25
#define HIGH_VIRT_BASE_POLICY 0xFFFFFF8000000000UL /* canonical for 39-bit */
#endif

/** Memory attributes indices for MAIR register */
#define ATTR_IDX_DEVICE_nGnRnE 0  // Device, non-Gathering, non-Reordering, no Early write ack
#define ATTR_IDX_NORMAL       1  // Normal memory, Inner/Outer Write-Back Cacheable
#define ATTR_IDX_NORMAL_NC    2  // Normal memory, non-cacheable
#define ATTR_IDX_DEVICE_nGnRE 3  // Device, non-Gathering, non-Reordering, Early write ack

/** ARMv8 Memory Region Attributes (used in MAIR_EL1 register) */
#define MAIR_ATTR_DEVICE_nGnRnE 0x00  // Device: non-Gathering, non-Reordering, non-Early Write Ack
#define MAIR_ATTR_DEVICE_nGnRE  0x04  // Device: non-Gathering, non-Reordering, Early Write Ack
#define MAIR_ATTR_NORMAL_NC     0x44  // Normal Memory: NC, NC
#define MAIR_ATTR_NORMAL        0xFF  // Normal Memory: WB RA/WA, WB RA/WA

/* ========================================================================
 * PAGE TABLE ENTRY FLAGS (Mechanics - Used by page table construction)
 * ======================================================================== */

/** Memory Type Attributes for ARMv8 */
#define PTE_ATTRINDX(idx)   ((idx) << 2)  // Shift attribute index to appropriate bits [4:2]
#define PTE_NORMAL          PTE_ATTRINDX(ATTR_IDX_NORMAL)
#define PTE_NORMAL_NC       PTE_ATTRINDX(ATTR_IDX_NORMAL_NC)
#define PTE_DEVICE_nGnRnE   PTE_ATTRINDX(ATTR_IDX_DEVICE_nGnRnE)
#define PTE_DEVICE_nGnRE    PTE_ATTRINDX(ATTR_IDX_DEVICE_nGnRE)

/** Access Permissions */
#define PTE_AP_RW       (0UL << 6)   // Read-Write for EL1, no access for EL0
#define PTE_AP_RO       (1UL << 6)   // Read-Only for EL1, no access for EL0
#define PTE_AP_RW_EL0   (1UL << 7 | 0UL << 6)   // Read-Write for EL1 and EL0
#define PTE_AP_RO_EL0   (1UL << 7 | 1UL << 6)   // Read-Only for EL1 and EL0
#define PTE_AP_USER     (1UL << 7)   // Add EL0 access when set - for backward compatibility
#define PTE_AP_MASK     (3UL << 6)   // Access Permission mask (bits 6-7)

/** Execute permissions - Execute Never bits */
#define PTE_UXN         (1UL << 54)  // Unprivileged Execute Never (EL0 can't execute)
#define PTE_PXN         (1UL << 53)  // Privileged Execute Never (EL1 can't execute)
#define PTE_NOEXEC      (PTE_UXN | PTE_PXN)  // No execution at any level

/** Shareability attributes */
#define PTE_SH_NONE     (0UL << 8)   // Non-shareable
#define PTE_SH_OUTER    (2UL << 8)   // Outer shareable
#define PTE_SH_INNER    (3UL << 8)   // Inner shareable

/** Address masks */
#define PTE_TABLE_ADDR  (~0xFFFUL)   // Address mask for table entries (bits [47:12])
#define PTE_ADDR_MASK   (~0xFFFUL)   // Physical address mask for page table entries (bits [47:12])

/** Combined flags for typical memory regions */
#define PTE_KERN_DATA   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW | PTE_NOEXEC)
#define PTE_KERN_RODATA (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO | PTE_NOEXEC)
#define PTE_KERN_TEXT   (PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO)

#define PTE_USER_DATA   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW_EL0 | PTE_NOEXEC)
#define PTE_USER_RODATA (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO_EL0 | PTE_NOEXEC)
#define PTE_USER_TEXT   (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RO_EL0 | PTE_UXN)

/** Additional flag combinations for MMIO regions */
#define PTE_DEVICE      (PTE_VALID | PTE_AF | PTE_SH_OUTER | PTE_DEVICE_nGnRE | PTE_AP_RW | PTE_NOEXEC)

/** Additional definitions for backward compatibility */
#define PTE_KERNEL_EXEC (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_NORMAL | PTE_AP_RW)  // Kernel executable memory
#define PTE_EXEC        (0UL)       // Executable at all levels (both UXN/PXN clear)
#define ATTR_NORMAL_EXEC  PTE_NORMAL  // Normal memory with execution permitted
#define PTE_ACCESS      PTE_AF      // Simplified alias for the Access Flag

/** Explicit inverse flags for readability */
#define PTE_PXN_DISABLE (0UL)  // Allow privilege execution (kernel can execute)
#define PTE_UXN_DISABLE (0UL)  // Allow unprivileged execution (user can execute)

/* ========================================================================
 * SYSTEM REGISTER ACCESS MACROS
 * ======================================================================== */

/** MSR/MRS macros for system register access */
#define MSR(reg, val) __asm__ volatile("msr " reg ", %0" :: "r" (val))
#define MRS(reg, val) __asm__ volatile("mrs %0, " reg : "=r" (val))

/* ========================================================================
 * DEBUG AND HARDWARE CONSTANTS
 * ======================================================================== */

/** Debug UART for direct output even when system is unstable */
#define DEBUG_UART 0x09000000

/** Kernel stack configuration */
#define KERNEL_STACK_VA 0x400FF000  // Fixed virtual address for kernel stack
#define KERNEL_STACK_PATTERN 0xDEADBEEF

/* ========================================================================
 * TYPE DEFINITIONS
 * ======================================================================== */

/**
 * @brief Structure to hold both virtual and physical addresses for page tables
 * 
 * This structure is used to maintain references to page tables where both
 * the virtual and physical addresses are important (e.g., before and after
 * MMU activation).
 */
typedef struct {
    uint64_t* virt;  /**< Virtual address of the page table */
    uint64_t  phys;  /**< Physical address of the page table */
} PageTableRef;

/**
 * @brief Structure to store memory mapping information for diagnostics
 * 
 * This structure stores information about memory mappings for debugging
 * and diagnostic purposes. It tracks virtual-to-physical mappings along
 * with their attributes.
 */
typedef struct {
    uint64_t virt_start;    /**< Starting virtual address */
    uint64_t virt_end;      /**< Ending virtual address */
    uint64_t phys_start;    /**< Starting physical address */
    uint64_t flags;         /**< Page table entry flags */
    const char* name;       /**< Human-readable name for this mapping */
} MemoryMapping;

/** Maximum number of memory mappings that can be tracked */
#define MAX_MAPPINGS 32

/* ========================================================================
 * GLOBAL VARIABLE DECLARATIONS
 * ======================================================================== */

/** Global page table pointers */
extern uint64_t* l0_table;                   /**< Main L0 page table (TTBR0) */
extern uint64_t* l0_table_ttbr1;             /**< Separate L0 page table (TTBR1) */
extern uint64_t saved_vector_table_addr;     /**< Preserved vector table address */

/** Memory mapping tracking */
extern MemoryMapping mappings[MAX_MAPPINGS]; /**< Array of tracked memory mappings */
extern int num_mappings;                     /**< Number of currently tracked mappings */

/** Debug and configuration flags */
extern bool debug_vmm;                       /**< Debug flag for VMM operations */
extern bool mmu_enabled;                     /**< MMU enabled status flag */

/* ========================================================================
 * FUNCTION PROTOTYPES
 * ======================================================================== */

/**
 * @brief Function to safely write to physical memory with proper cache maintenance
 * @param phys_addr Physical address to write to
 * @param value Value to write
 */
static inline void write_phys64(uint64_t phys_addr, uint64_t value) {
    volatile uint64_t* addr = (volatile uint64_t*)phys_addr;
    *addr = value;
    
    // Data Cache Clean by VA to Point of Coherency
    asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
    
    // Full system Data Synchronization Barrier
    asm volatile("dsb sy" ::: "memory");
    
    // Instruction Synchronization Barrier
    asm volatile("isb" ::: "memory");
}

/** System register access functions */
uint64_t read_mair_el1(void);
uint64_t read_ttbr1_el1(void);
uint64_t read_vbar_el1(void);

/** Page table management functions */
uint64_t* get_l3_table_for_addr(uint64_t* l0_table, uint64_t virt_addr);
uint64_t* get_kernel_page_table(void);
uint64_t* get_kernel_ttbr1_page_table(void);
uint64_t* init_page_tables(void);

/** Memory mapping functions */
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags);
void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
               uint64_t phys_start, uint64_t flags);
void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, 
                     uint64_t flags, const char* name);

/** MMU control functions */
void enable_mmu(uint64_t* page_table_base);
void enable_mmu_enhanced(uint64_t* page_table_base);
void map_vector_table_dual(uint64_t* l0_table_ttbr0, uint64_t* l0_table_ttbr1, 
                           uint64_t vector_addr);
void verify_critical_mappings_before_mmu(uint64_t* page_table_base);
void enhanced_cache_maintenance(void);

/** Debug and diagnostic functions */
void debug_hex64_mmu(const char* label, uint64_t value);
void verify_page_mapping(uint64_t va);
void audit_memory_mappings(void);
void flush_cache_lines(void* addr, size_t size);

/** MMU continuation point */
void __attribute__((used, aligned(4096))) mmu_continuation_point(void);

#endif /* MEMORY_CONFIG_H */ 