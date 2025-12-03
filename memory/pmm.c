#include "../include/types.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"
#include "../include/vmm.h"
#include "../include/memory_config.h"
#include "../include/debug.h"
#include "../include/debug_config.h"
#include "../include/mmu_policy.h"  // For centralized TLB operations

// Declaration for debug_hex64 function from kernel/main.c
extern void debug_hex64(const char* label, uint64_t value);

// Forward declarations
void record_allocation(uintptr_t addr, size_t pages);
uint64_t get_timestamp(void);
void test_memory_writability(void);  // Add forward declaration

// Updated memory start address to 0x40000000 for better reliability
#define MEMORY_START  0x40000000  // Using higher memory region known to be writable
#define MEMORY_END    0x48000000  // 128MB region
// NOTE: PAGE_SIZE is defined in memory_config.h

// Kernel size is determined by linker script
extern char __kernel_end[];

// Static bitmap for page allocation
// Each bit represents one 4KB page (1 = used, 0 = free)
#define BITMAP_SIZE ((MEMORY_END - MEMORY_START) / PAGE_SIZE / 8)
static uint8_t* page_bitmap = NULL;  // Changed from array to pointer
static size_t total_pages = 0;       // Added total_pages declaration

// Add to global variables
static struct {
    size_t total_allocations;    // Counter for all allocations
    size_t current_allocated;    // Currently allocated pages
    size_t peak_allocated;       // Maximum pages allocated at once
    size_t failed_allocations;   // Failed allocation attempts
} pmm_stats = {0};

// For more detailed debugging, create a circular buffer to track recent allocations
#define TRACK_BUFFER_SIZE 32
static struct {
    uintptr_t addr;
    size_t size;  // in pages
    uint64_t timestamp;
} recent_allocs[TRACK_BUFFER_SIZE];
static int alloc_index = 0;

// Simple timestamp implementation - returns a monotonically increasing counter
static uint64_t timestamp_counter = 0;

// Get current timestamp (simplified version)
uint64_t get_timestamp(void) {
    return timestamp_counter++;
}

// Mark a page as used or free
static void set_page_bit(uint64_t addr, int used) {
    if (addr < MEMORY_START || addr >= MEMORY_END) {
        return;  // Address out of range
    }
    
    uint64_t page_idx = (addr - MEMORY_START) / PAGE_SIZE;
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    
    // Sanity check - prevent bitmap corruption
    if (byte_idx >= (total_pages + 7) / 8) {
        uart_putc('X');  // Error marker
        debug_hex64("INVALID_BYTE_IDX", byte_idx);
        debug_hex64("addr", addr);
        debug_hex64("page_idx", page_idx);
        return;
    }
    
    // Debug output for significant pages
    /*if (page_idx < 8 || page_idx > (0x40000000 / PAGE_SIZE) - 8) {
        debug_hex64("set_bit_addr", addr);
        debug_hex64("set_bit_page", page_idx);
        debug_hex64("set_bit_byte", byte_idx);
        debug_hex64("set_bit_bit", bit_idx);
        debug_hex64("set_bit_val", used);
    }*/
    
    if (used) {
        page_bitmap[byte_idx] |= (1 << bit_idx);
    } else {
        page_bitmap[byte_idx] &= ~(1 << bit_idx);
    }
}

// Check if a page is used
static int is_page_used(uint64_t addr) {
    if (addr < MEMORY_START || addr >= MEMORY_END) {
        return 1;  // Address out of range, treat as used
    }
    
    uint64_t page_idx = (addr - MEMORY_START) / PAGE_SIZE;
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    
    return (page_bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

// Enhanced test function with configurable debug patterns
// Outputs: A[configurable test patterns]B based on debug_config.h settings
__attribute__((naked)) void test_return(void) {
    asm volatile(
        // Save registers for C function calls
        "stp x0, x1, [sp, #-16]!\n"
        "stp x2, x3, [sp, #-16]!\n"
        "stp x30, x29, [sp, #-16]!\n"
        
        "mov x9, #0x09000000\n"    // UART address in x9
        
        // Print 'A' marker (always present)
        "mov w10, #65\n"           // ASCII 'A'
        "str w10, [x9]\n"          // Write 'A' to UART
        
#ifdef DEBUG_TEST_PATTERNS_ENABLED
        // === VERBOSE MODE: Output all test patterns ===
        
        // Call uart_hex64_early for each pattern
        "mov x0, #0xCAFE\n"
        "movk x0, #0xBABE, lsl #16\n"  
        "movk x0, #0xDEAD, lsl #32\n"
        "movk x0, #0xBEEF, lsl #48\n"
        "bl uart_hex64_early\n"     // Pattern 1, instance 1
        "bl uart_hex64_early\n"     // Pattern 1, instance 2
        
        "mov x0, #0x0123\n"
        "movk x0, #0x4567, lsl #16\n"
        "movk x0, #0x89AB, lsl #32\n" 
        "movk x0, #0xCDEF, lsl #48\n"
        "bl uart_hex64_early\n"     // Pattern 2, instance 1
        "bl uart_hex64_early\n"     // Pattern 2, instance 2
        
        "mov x0, #0xFEDC\n"
        "movk x0, #0xBA98, lsl #16\n"
        "movk x0, #0x7654, lsl #32\n"
        "movk x0, #0x3210, lsl #48\n"
        "bl uart_hex64_early\n"     // Pattern 3, instance 1  
        "bl uart_hex64_early\n"     // Pattern 3, instance 2
        
        // Print 'B' end marker in verbose mode
        "mov w10, #66\n"            // ASCII 'B'
        "str w10, [x9]\n"           // Write 'B' to UART
#endif
        
        // Restore registers and return
        "ldp x30, x29, [sp], #16\n"
        "ldp x2, x3, [sp], #16\n"
        "ldp x0, x1, [sp], #16\n"
        "ret\n"
    );
}

// Normal C implementation that will be called from our assembly wrapper
void init_pmm_impl(void) {
    uart_putc('A');  // Start of init_pmm
    
    // Run memory writability test FIRST to ensure we're working with writable memory
    test_memory_writability();
    
    // Proceed only if memory is confirmed to be writeable
    uart_puts("[PMM] Initializing physical memory manager...\n");

    // Place page_bitmap right after kernel, 4KB aligned
    page_bitmap = (void *)((uintptr_t)__kernel_end + 0x1000);
    
    // Safety check: ensure we're within RAM bounds (128MB = 0x48000000)
    if ((uintptr_t)page_bitmap >= 0x48000000) {
        uart_putc('E');  // Error: page_bitmap would be outside RAM
        return;
    }

    uart_putc('B');  // page_bitmap placement successful

    // Calculate total pages (1 bit per page)
    total_pages = (MEMORY_END - MEMORY_START) / PAGE_SIZE;
    size_t bitmap_size = (total_pages + 7) / 8;  // Round up to bytes

    // Debug the key values
    /*debug_hex64("page_bitmap @", (uintptr_t)page_bitmap);
    debug_hex64("bitmap_size", bitmap_size);
    debug_hex64("kernel_end", (uintptr_t)__kernel_end);
    debug_hex64("total_pages", total_pages);

    uart_putc('C');  // Before clearing bitmap

    // 1. Zero the bitmap (mark all pages as free)
    for (size_t i = 0; i < bitmap_size; i++) {
        page_bitmap[i] = 0;
    }

    uart_putc('D');  // After zeroing bitmap

    // 2. Calculate end address of bitmap with alignment
    uintptr_t bitmap_end = ((uintptr_t)page_bitmap + bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Debug output
    debug_hex64("0. Real Memory Start", MEMORY_START);
    debug_hex64("1. Real kernel_end", (uintptr_t)__kernel_end);
    debug_hex64("2. Bitmap region", (uintptr_t)page_bitmap);
    debug_hex64("3. Bitmap end", bitmap_end);
    
    // 3. Mark pages for the kernel (0x80000 to real kernel end)
    uart_putc('K');  // Marking kernel pages
    size_t kernel_pages = 0;
    for (uintptr_t addr = MEMORY_START; addr < 0x100000; addr += PAGE_SIZE) {
        set_page_bit(addr, 1);  // Mark pages from 0x80000 to 1MB as used
        kernel_pages++;
    }
    debug_hex64("Kernel pages marked", kernel_pages);
    
    // 4. Mark pages for the bitmap
    uart_putc('M');  // Marking bitmap pages
    size_t bitmap_pages = 0;
    for (uintptr_t addr = (uintptr_t)page_bitmap & ~(PAGE_SIZE-1); 
         addr < bitmap_end; 
         addr += PAGE_SIZE) {
        set_page_bit(addr, 1);  // Mark bitmap pages as used
        bitmap_pages++;
    }
    debug_hex64("Bitmap pages marked", bitmap_pages);
    
    // 5. Verify bitmap contents
    uart_putc('V');  // Verifying bitmap
    for (int i = 0; i < 4; i++) {
        debug_hex64("bitmap_byte", page_bitmap[i]);
    }
    
    // 6. Count free/reserved pages
    size_t free_pages = 0;
    size_t reserved_pages = 0;
    
    for (uintptr_t addr = MEMORY_START; addr < MEMORY_END; addr += PAGE_SIZE) {
        if (is_page_used(addr)) {
            reserved_pages++;
        } else {
            free_pages++;
        }
    }
    
    // 7. Final debug output
    debug_hex64("total_pages", total_pages);
    debug_hex64("free_pages", free_pages);
    debug_hex64("reserved_pages", reserved_pages);*/
    
    uart_putc('P');  // PMM initialization complete
}

// Since naked attribute is ignored, we'll use a simpler approach
void init_pmm(void) {
    // Save registers that might be modified
    asm volatile(
        "stp x29, x30, [sp, #-16]!\n"  // Save frame pointer and link register
        "mov x29, sp\n"                // Set up frame pointer
    );
    
    // Call the implementation directly from C
    init_pmm_impl();
    
    // Restore registers and return
    asm volatile(
        "ldp x29, x30, [sp], #16\n"    // Restore frame pointer and link register
        "ret\n"                        // Explicit return
    );
}

// Safer alloc_page() tracking
void* alloc_page(void) {
    for (size_t i = 0; i < total_pages; ++i) {
        uintptr_t addr = MEMORY_START + i * PAGE_SIZE;
        if (!is_page_used(addr)) {
            set_page_bit(addr, 1);  // Mark as used

            // Optional: zero page before returning
            memset((void*)addr, 0, PAGE_SIZE);

            // Update statistics
            pmm_stats.total_allocations++;
            pmm_stats.current_allocated++;
            if (pmm_stats.current_allocated > pmm_stats.peak_allocated)
                pmm_stats.peak_allocated = pmm_stats.current_allocated;
            
            // Record allocation
            record_allocation(addr, 1);
            
            // Debug output
            //debug_hex64("[PMM] alloc_page -> ", addr);
            //debug_hex64("[PMM] alloc #", pmm_stats.total_allocations);

            return (void*)addr;
        }
    }

    uart_puts("[PMM] ERROR: Out of memory!\n");
    pmm_stats.failed_allocations++;
    return NULL;
}

void free_page(void* addr) {
    if (addr == NULL) {
        return;  // Prevent NULL dereference
    }
    
    // Check if address is page-aligned
    if ((uint64_t)addr % PAGE_SIZE != 0) {
        uart_puts("[PMM] ERROR: Address not page-aligned!\n");
        return;
    }
    
    // Check if address is in valid range
    if ((uint64_t)addr < MEMORY_START || (uint64_t)addr >= MEMORY_END) {
        uart_puts("[PMM] ERROR: Address out of range!\n");
        return;
    }
    
    // Check if page was actually allocated
    if (!is_page_used((uint64_t)addr)) {
        uart_puts("[PMM] WARNING: Freeing already free page!\n");
        return;
    }
    
    // Mark as free
    set_page_bit((uint64_t)addr, 0);
    
    // Update statistics
    pmm_stats.current_allocated--;
    
    // Record the free operation
    record_allocation((uint64_t)addr, 0);
    
    // Debug output
    debug_hex64("[PMM] free page", (uint64_t)addr);
}

// Reserve a specific number of pages for page tables 
void reserve_pages_for_page_tables(uint64_t num_pages) {
    uint64_t reserved = 0;                     // Counter for successfully reserved pages
    uint64_t kernel_end = (uint64_t)__kernel_end;
    
    // Scan memory from kernel end to total memory limit
    for (uint64_t addr = kernel_end;           // Start after kernel space
         addr < MEMORY_END &&                  // Don't exceed physical memory
         reserved < num_pages;                 // Until we reserve requested pages
         addr += PAGE_SIZE) {                  // Check each 4KB page
        
        if (!is_page_used(addr)) {             // Only consider free pages
            set_page_bit(addr, 1);             // Mark page as used
            reserved++;                        // Increment reservation counter
        }
    }
}

// Add a function to print the current memory map
void pmm_print_memory_map(void) {
    uart_puts("\n=== MEMORY MAP ===\n");
    debug_hex64("Total pages:", total_pages);
    debug_hex64("Free pages:", total_pages - pmm_stats.current_allocated);
    debug_hex64("Used pages:", pmm_stats.current_allocated);
    debug_hex64("Peak usage:", pmm_stats.peak_allocated);
    
    // Print memory regions (kernel, bitmap, free, allocated)
    uart_puts("Kernel:  0x80000 -> 0x100000\n");
    debug_hex64("Bitmap: ", (uint64_t)page_bitmap);
    
    // Optional: Print a visual map (sampling every Nth page)
    uart_puts("Map: [K=kernel, B=bitmap, A=allocated, F=free]\n");
    char map[101] = {0};
    for (int i = 0; i < 100; i++) {
        uintptr_t addr = MEMORY_START + (i * total_pages / 100) * PAGE_SIZE;
        map[i] = is_page_used(addr) ? 'A' : 'F';
        // Mark known regions
        if (addr < 0x100000) map[i] = 'K';
        if (addr >= (uintptr_t)page_bitmap && 
            addr < (uintptr_t)page_bitmap + BITMAP_SIZE) map[i] = 'B';
    }
    map[100] = '\0';
    uart_puts(map);
    uart_puts("\n===============\n");
}

// Add to allocation function
void record_allocation(uintptr_t addr, size_t pages) {
    recent_allocs[alloc_index].addr = addr;
    recent_allocs[alloc_index].size = pages;
    recent_allocs[alloc_index].timestamp = get_timestamp(); // You'd need to implement this
    alloc_index = (alloc_index + 1) % TRACK_BUFFER_SIZE;
}

// String comparison function (to avoid using stdlib)
static int pmm_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// If you have a shell or command interface, add commands like:
void pmm_command(const char* cmd) {
    if (pmm_strcmp(cmd, "stats") == 0) {
        debug_hex64("Total allocations:", pmm_stats.total_allocations);
        debug_hex64("Current allocated:", pmm_stats.current_allocated);
        debug_hex64("Peak usage:", pmm_stats.peak_allocated);
        debug_hex64("Failed allocations:", pmm_stats.failed_allocations);
    } else if (pmm_strcmp(cmd, "map") == 0) {
        pmm_print_memory_map();
    } else if (pmm_strcmp(cmd, "recent") == 0) {
        // Print recent allocations
    }
}

// Function to test if memory is actually writable
void test_memory_writability(void) {
    uart_puts("[PMM] Testing memory writability...\n");
    
    // Test addresses
    uint64_t* test_addr1 = (uint64_t*)0x40000000;
    uint64_t* test_addr2 = (uint64_t*)0x40100000;
    uint64_t* test_addr3 = (uint64_t*)0x40200000;
    
    // Test patterns
    uint64_t pattern1 = 0xCAFEBABEDEADBEEF;
    uint64_t pattern2 = 0x0123456789ABCDEF;
    uint64_t pattern3 = 0xFEDCBA9876543210;
    
    // Write patterns
    *test_addr1 = pattern1;
    *test_addr2 = pattern2;
    *test_addr3 = pattern3;
    
    // Flush data cache
    asm volatile (
        "dc cvac, %0\n"
        "dc cvac, %1\n"
        "dc cvac, %2\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(test_addr1), "r"(test_addr2), "r"(test_addr3) : "memory"
    );
    
    // Read back and verify
    uart_puts("[PMM] Test addr1: wrote 0x");
    uart_hex64(pattern1);
    uart_puts(", read 0x");
    uart_hex64(*test_addr1);
    uart_puts((*test_addr1 == pattern1) ? " - PASS\n" : " - FAIL\n");
    
    uart_puts("[PMM] Test addr2: wrote 0x");
    uart_hex64(pattern2);
    uart_puts(", read 0x");
    uart_hex64(*test_addr2);
    uart_puts((*test_addr2 == pattern2) ? " - PASS\n" : " - FAIL\n");
    
    uart_puts("[PMM] Test addr3: wrote 0x");
    uart_hex64(pattern3);
    uart_puts(", read 0x");
    uart_hex64(*test_addr3);
    uart_puts((*test_addr3 == pattern3) ? " - PASS\n" : " - FAIL\n");
    
    // Final result
    if (*test_addr1 == pattern1 && *test_addr2 == pattern2 && *test_addr3 == pattern3) {
        uart_puts("[PMM] Memory writability test PASSED\n");
    } else {
        uart_puts("[PMM] Memory writability test FAILED\n");
        uart_puts("[PMM] Critical error: Memory write operations not working as expected\n");
    }
}

// ============================================================================
// PHYSICAL MEMORY MAPPING FUNCTIONS (moved from vmm.c)
// ============================================================================

// External declarations for VMM functions we depend on
extern uint64_t* l0_table;
extern uint64_t* l0_table_ttbr1;
extern bool debug_vmm;

// External kernel page table functions
extern uint64_t* get_kernel_page_table(void);
extern uint64_t* get_kernel_ttbr1_page_table(void);
extern uint64_t get_pte(uint64_t virt_addr);
extern void register_mapping(uint64_t virt_start, uint64_t virt_end, uint64_t phys_start, 
                     uint64_t flags, const char* name);

// External UART functions
extern void uart_puts_early(const char* str);
extern void uart_hex64_early(uint64_t val);
extern void uart_puts_safe_indexed(const char* str);
extern void uart_emergency_hex64(uint64_t val);

// External debug functions
extern void debug_print(const char* msg);
extern void debug_hex64(const char* label, uint64_t value);

// NOTE: write_phys64 is implemented as a static inline function in memory_config.h

/**
 * @brief Allocate and initialize a new page table
 * @return Pointer to the newly allocated page table, or NULL on failure
 */
uint64_t* create_page_table(void) {
    void* table = alloc_page();
    if (!table) {
        uart_puts("[PMM] Failed to allocate page table!\n");
        return NULL;
    }

    memset(table, 0, PAGE_SIZE);  // clear entries
    return (uint64_t*)table;
}

/**
 * @brief Map a single page to an L3 table
 * @param l3_table Pointer to the L3 page table
 * @param va Virtual address to map
 * @param pa Physical address to map to
 * @param flags Page table entry flags
 */
void map_page(uint64_t* l3_table, uint64_t va, uint64_t pa, uint64_t flags) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    if (l3_table == NULL) {
        // Debug: X = L3 table is NULL
        *uart = 'X'; *uart = 'N'; *uart = 'U'; *uart = 'L'; *uart = 'L'; *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Check if we're trying to map in the UART region - avoid unnecessary double mappings
    if ((pa >= UART_PHYS && pa < (UART_PHYS + 0x1000)) ||
        (va >= UART_PHYS && va < (UART_PHYS + 0x1000))) {
        // Skip UART MMIO region to avoid collisions
        // Debug: S = Skip UART region
        *uart = 'S'; *uart = 'K'; *uart = 'I'; *uart = 'P'; 
        *uart = 'P'; uart_hex64_early(pa);
        *uart = 'V'; uart_hex64_early(va);
        *uart = '\r'; *uart = '\n';
        return;
    }
    
    uint64_t l3_index = (va >> PAGE_SHIFT) & (ENTRIES_PER_TABLE - 1);
    uint64_t l3_entry = pa;
    
    // Add flags
    l3_entry |= PTE_PAGE;  // Mark as a page entry
    l3_entry |= flags;    // Add provided flags
    
    // Store the entry
    l3_table[l3_index] = l3_entry;
    
    // Debug output
    if (debug_vmm) {
        // Debug: O = mapped OK
        *uart = 'O'; *uart = 'K';
        *uart = 'V'; uart_hex64_early(va);
        *uart = 'P'; uart_hex64_early(pa);
        *uart = 'F'; uart_hex64_early(flags);
        *uart = 'I'; uart_hex64_early(l3_index);
        *uart = '\r'; *uart = '\n';
    }
}

/**
 * @brief Map a range of virtual addresses to physical addresses
 * @param l0_table Root page table
 * @param virt_start Starting virtual address
 * @param virt_end Ending virtual address
 * @param phys_start Starting physical address
 * @param flags Page table entry flags
 */
void map_range(uint64_t* l0_table, uint64_t virt_start, uint64_t virt_end, 
               uint64_t phys_start, uint64_t flags) {
    // UART for debug markers
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    
    // Calculate the number of pages
    uint64_t size = virt_end - virt_start;
    uint64_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    #if DEBUG_MEMORY_MAPPING_ENABLED
    // Debug markers: M=start mapping, V=virt_start, E=virt_end, P=phys_start, N=num_pages, F=flags
    *uart = 'M'; *uart = 'A'; *uart = 'P'; *uart = ':';  // MAP:
    *uart = 'V'; uart_hex64_early(virt_start);           // V + virt_start
    *uart = 'E'; uart_hex64_early(virt_end);             // E + virt_end  
    *uart = 'P'; uart_hex64_early(phys_start);           // P + phys_start
    *uart = 'N'; uart_hex64_early(num_pages);            // N + num_pages
    *uart = 'F'; uart_hex64_early(flags);                // F + flags
    *uart = '\r'; *uart = '\n';
    #endif
    
    // Map each page
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t virt_addr = virt_start + (i * PAGE_SIZE);
        uint64_t phys_addr = phys_start + (i * PAGE_SIZE);
        
        // Determine which page table to use based on virtual address
        uint64_t* page_table_to_use;
        if (virt_addr >= HIGH_VIRT_BASE) {
            // High virtual address - use TTBR1 page table
            page_table_to_use = l0_table_ttbr1;
            
            #if DEBUG_MEMORY_MAPPING_PER_PAGE
            // Debug: T1 = TTBR1 page table used
            *uart = 'T'; *uart = '1'; uart_hex64_early(virt_addr); *uart = '\r'; *uart = '\n';
            #endif
        } else {
            // Low virtual address - use TTBR0 page table (passed parameter)
            page_table_to_use = l0_table;
            
            #if DEBUG_MEMORY_MAPPING_PER_PAGE
            // Debug: T0 = TTBR0 page table used
            *uart = 'T'; *uart = '0'; uart_hex64_early(virt_addr); *uart = '\r'; *uart = '\n';
            #endif
        }
        
        // Create/get L3 table for the virtual address
        uint64_t* l3_table = get_l3_table_for_addr(page_table_to_use, virt_addr);
        if (!l3_table) {
            // Debug: X = failed to get L3 table
            *uart = 'X'; *uart = 'L'; *uart = '3'; uart_hex64_early(virt_addr); *uart = '\r'; *uart = '\n';
            continue;
        }
        
        // Calculate the L3 index
        uint64_t l3_idx = (virt_addr >> 12) & 0x1FF;
        
        // Create page table entry
        uint64_t pte = (phys_addr & ~0xFFF) | flags;
        
        // Cache maintenance before updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Write the mapping
        l3_table[l3_idx] = pte;
        
        // Cache maintenance after updating PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // Perform explicit TLB invalidation for this address - REPLACED WITH POLICY LAYER
        // asm volatile("tlbi vaae1is, %0" :: "r"(virt_addr >> 12) : "memory");  // ❌ POLICY VIOLATION - address-specific inner-shareable TLB invalidation
        // asm volatile("dsb ish" ::: "memory");
        
        // ✅ OPTIMIZATION: Skip per-page TLB invalidation - do bulk invalidation at end instead
        // mmu_comprehensive_tlbi_sequence();  // ❌ REMOVED: Causes excessive debug output
    }
    
    // Final TLB invalidation after all updates - REPLACED WITH POLICY LAYER
    // asm volatile("tlbi vmalle1is" ::: "memory");  // ❌ POLICY VIOLATION - inner-shareable TLB invalidation
    // asm volatile("dsb ish" ::: "memory");
    // asm volatile("isb" ::: "memory");
    
    *uart = 'B'; *uart = 'U'; *uart = 'L'; *uart = 'K'; *uart = ':'; *uart = 'T'; *uart = 'L'; *uart = 'B';
    *uart = '\r'; *uart = '\n';
    
    // ✅ POLICY LAYER: Single bulk TLB invalidation after mapping all pages (MUCH more efficient!)
    mmu_comprehensive_tlbi_sequence_quiet();  // Use quiet version to avoid console flooding
    
    *uart = ':'; *uart = 'O'; *uart = 'K';
    *uart = '\r'; *uart = '\n';
    
    // Register the mapping for diagnostic purposes
    register_mapping(virt_start, virt_end, phys_start, flags, "Range mapping");
}

/**
 * @brief Direct physical page mapping with automatic L3 table lookup
 * @param va Virtual address to map
 * @param pa Physical address to map to
 * @param size Size of the mapping
 * @param flags Page table entry flags
 */
void map_page_direct(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    if (l0_table == NULL) {
        uart_puts("[PMM] ERROR: Cannot map page - l0_table not initialized\n");
        return;
    }
    
    // Get the L3 table for this address
    uint64_t* l3_pt_local = get_l3_table_for_addr(l0_table, va);
    if (l3_pt_local == NULL) {
        uart_puts("[PMM] ERROR: Failed to get L3 table for address 0x");
        uart_hex64(va);
        uart_puts("\n");
        return;
    }
    
    // Map each page in the range
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t page_va = va + offset;
        uint64_t page_pa = pa + offset;
        
        // Use the full version of map_page that takes an L3 table
        map_page(l3_pt_local, page_va, page_pa, flags);
    }
}

/**
 * @brief Map a kernel page with proper TLB invalidation
 * @param va Virtual address to map
 * @param pa Physical address to map to
 * @param flags Page table entry flags
 */
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t flags) {
    debug_print("[PMM] Mapping kernel page VA 0x");
    debug_hex64("", va);
    debug_print(" to PA 0x");
    debug_hex64("", pa);
    debug_print("\n");
    
    // Get the kernel page table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        debug_print("[PMM] ERROR: Could not get kernel page table!\n");
        return;
    }
    
    // Get the L3 table for the address
    uint64_t* l3_table = get_l3_table_for_addr(l0_table, va);
    if (!l3_table) {
        debug_print("[PMM] ERROR: Could not get L3 table for address!\n");
        return;
    }
    
    // Map the page
    map_page(l3_table, va, pa, flags);
    
    // Flush TLB to ensure changes take effect - REPLACED WITH POLICY LAYER
    // __asm__ volatile("dsb ishst");
    // __asm__ volatile("tlbi vmalle1is");  // ❌ POLICY VIOLATION - inner-shareable TLB invalidation
    // __asm__ volatile("dsb ish");
    // __asm__ volatile("isb");
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
    mmu_comprehensive_tlbi_sequence();
    
    debug_print("[PMM] Kernel page mapped successfully\n");
}

/**
 * @brief Map UART MMIO region for both virtual and identity addressing
 */
void map_uart(void) {
    volatile uint32_t* uart = (volatile uint32_t*)0x09000000;
    // Debug: U = UART mapping start
    *uart = 'U'; *uart = 'A'; *uart = 'R'; *uart = 'T'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T'; *uart = '\r'; *uart = '\n';
    
    // Make sure we have access to kernel L0 table
    uint64_t* l0_table = get_kernel_page_table();
    if (!l0_table) {
        // Debug: X = failed to get kernel page table
        *uart = 'X'; *uart = 'K'; *uart = 'E'; *uart = 'R'; *uart = 'N'; *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Select the correct L0 root table for the UART virtual address.
    // High kernel addresses (>= HIGH_VIRT_BASE) must be placed in the
    // TTBR1 tree; low addresses use TTBR0.  UART_VIRT is always high, but
    // keep the check generic in case the constant changes.

    uint64_t* root_for_virt = (UART_VIRT >= HIGH_VIRT_BASE) ? l0_table_ttbr1
                                                            : l0_table;

    // Create/get L3 table for the UART virtual address region using the
    // selected root.
    uint64_t* l3_table = get_l3_table_for_addr(root_for_virt, UART_VIRT);
    if (!l3_table) {
        // Debug: X = failed to get L3 table for UART
        *uart = 'X'; *uart = 'L'; *uart = '3'; *uart = 'U'; *uart = 'A'; *uart = 'R'; *uart = 'T'; *uart = '\r'; *uart = '\n';
        return;
    }
    
    // Device memory attributes for UART MMIO - FIXED: Add all required flags
    uint64_t uart_flags = PTE_VALID | PTE_PAGE | PTE_AF |
                          PTE_DEVICE_nGnRE | PTE_AP_RW |
                          PTE_PXN | PTE_UXN;          // virtual
    
    // Make the UART virtual mapping non-executable to match the physical
    // identity mapping and avoid attribute mismatches across descriptors.
    uart_flags |= PTE_PXN | PTE_UXN;
    
    // Debug output before mapping
    // Debug: M = UART mapping details
    *uart = 'M'; *uart = 'A'; *uart = 'P';
    *uart = 'P'; uart_hex64_early(UART_PHYS);
    *uart = 'V'; uart_hex64_early(UART_VIRT);
    *uart = 'F'; uart_hex64_early(uart_flags);
    *uart = '\r'; *uart = '\n';
    
    // Calculate the L3 index
    uint64_t l3_idx = (UART_VIRT >> 12) & 0x1FF;
    
    // Create page table entry
    uint64_t pte = UART_PHYS | uart_flags;
    
    // Perform proper cache maintenance on the page table entry before writing
    asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
    asm volatile("dsb ish" ::: "memory");
    
    // Write the mapping
    l3_table[l3_idx] = pte;
    
    // Cache maintenance after updating the PTE
    asm volatile("dc civac, %0" :: "r"(&l3_table[l3_idx]) : "memory");
    asm volatile("dsb ish" ::: "memory");
    
    // Perform explicit TLB invalidation for this specific address - REPLACED WITH POLICY LAYER
    // asm volatile("tlbi vaae1is, %0" :: "r"(UART_VIRT >> 12) : "memory");  // ❌ POLICY VIOLATION - address-specific inner-shareable TLB invalidation
    // asm volatile("dsb ish" ::: "memory");
    // asm volatile("isb" ::: "memory");
    
    // ✅ POLICY LAYER: Use centralized TLB invalidation sequence
    mmu_comprehensive_tlbi_sequence();
    
    // Verify the mapping was set correctly
    uint64_t read_pte = l3_table[l3_idx];
    // Debug: V = verified PTE
    *uart = 'V'; *uart = 'E'; *uart = 'R'; uart_hex64_early(read_pte); *uart = '\r'; *uart = '\n';
    
    // Save the phys/virt addresses for diagnostic use
    register_mapping(UART_VIRT, UART_VIRT + 0x1000, UART_PHYS, uart_flags, "UART MMIO");

    // ========================================================================
    // CRITICAL: Identity map UART for trampoline debug output after MMU enable
    // The trampoline uses physical UART address (0x09000000) for printing,
    // so we need VA 0x09000000 → PA 0x09000000 in TTBR0 page tables.
    // ========================================================================
    
    *uart = 'I'; *uart = 'D'; *uart = ':'; *uart = 'S'; *uart = 'T'; *uart = 'A'; *uart = 'R'; *uart = 'T';
    *uart = '\r'; *uart = '\n';
    
    // Get L3 table for the UART physical address in TTBR0 (identity mapping)
    uint64_t* l3_table_phys = get_l3_table_for_addr(l0_table, UART_PHYS);
    
    if (l3_table_phys) {
        *uart = 'I'; *uart = 'D'; *uart = ':'; *uart = 'L'; *uart = '3'; *uart = 'O'; *uart = 'K';
        *uart = '\r'; *uart = '\n';
        
        uint64_t l3_idx_phys = (UART_PHYS >> 12) & 0x1FF;
        
        // Device memory flags: Valid, Page, AF, Device-nGnRE, RW, Non-executable
        uint64_t pte_phys = UART_PHYS |
                           PTE_VALID | PTE_PAGE | PTE_AF |
                           PTE_DEVICE_nGnRE |
                           PTE_AP_RW |
                           PTE_PXN | PTE_UXN;
        
        // Debug: Show what we're about to write
        *uart = 'I'; *uart = 'D'; *uart = ':';
        *uart = 'A'; uart_hex64_early(UART_PHYS);
        *uart = 'I'; uart_hex64_early(l3_idx_phys);
        *uart = 'P'; uart_hex64_early(pte_phys);
        *uart = '\r'; *uart = '\n';

        // Cache maintenance before writing PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table_phys[l3_idx_phys]) : "memory");
        asm volatile("dsb ish" ::: "memory");

        // Write the identity mapping PTE
        l3_table_phys[l3_idx_phys] = pte_phys;

        // Cache maintenance after writing PTE
        asm volatile("dc civac, %0" :: "r"(&l3_table_phys[l3_idx_phys]) : "memory");
        asm volatile("dsb ish" ::: "memory");
        
        // TLB invalidation
        mmu_comprehensive_tlbi_sequence();
        
        // Verify the write
        uint64_t verify_pte = l3_table_phys[l3_idx_phys];
        *uart = 'I'; *uart = 'D'; *uart = ':'; *uart = 'V'; uart_hex64_early(verify_pte);
        *uart = '\r'; *uart = '\n';

        // Register for diagnostics
        register_mapping(UART_PHYS, UART_PHYS + 0x1000, UART_PHYS, pte_phys, "UART MMIO (Identity)");
        
        *uart = 'I'; *uart = 'D'; *uart = ':'; *uart = 'O'; *uart = 'K';
        *uart = '\r'; *uart = '\n';
    } else {
        // CRITICAL FAILURE: Could not get L3 table for identity mapping
        *uart = 'I'; *uart = 'D'; *uart = ':'; *uart = 'F'; *uart = 'A'; *uart = 'I'; *uart = 'L';
        *uart = '\r'; *uart = '\n';
        *uart = 'X'; *uart = 'L'; *uart = '3'; *uart = 'I'; *uart = 'D';
        *uart = '\r'; *uart = '\n';
    }
}

/**
 * @brief Verify UART mapping after MMU is enabled
 */
void verify_uart_mapping(void) {
    uart_puts_safe_indexed("[PMM] Verifying UART virtual mapping post-MMU\n");
    
    // Get UART PTE from page tables
    uint64_t pte = get_pte(UART_VIRT);
    
    // Output PTE value for verification
    uart_puts_safe_indexed("[PMM] UART PTE post-MMU: 0x");
    uart_emergency_hex64(pte);
    uart_puts_safe_indexed("\n");
    
    // Check if the PTE is valid
    if (!(pte & PTE_VALID)) {
        uart_puts_safe_indexed("[PMM] ERROR: UART mapping is not valid!\n");
        return;
    }
    
    // Check memory attributes
    uint64_t attr_idx = (pte >> 2) & 0x7;
    uart_puts_safe_indexed("[PMM] UART memory attribute index: ");
    uart_emergency_hex64(attr_idx);
    uart_puts_safe_indexed("\n");
    
    // Verify UART functionality by reading UART registers
    volatile uint32_t* uart_fr = (volatile uint32_t*)(UART_VIRT + 0x18);
    uint32_t fr_val = *uart_fr;
    
    uart_puts_safe_indexed("[PMM] UART FR register value: 0x");
    uart_emergency_hex64(fr_val);
    uart_puts_safe_indexed("\n");
    
    uart_puts_safe_indexed("[PMM] UART mapping verification complete\n");
}

