#include "../include/types.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"

// Declaration for debug_hex64 function from kernel/main.c
extern void debug_hex64(const char* label, uint64_t value);

// Forward declarations
void record_allocation(uintptr_t addr, size_t pages);
uint64_t get_timestamp(void);
void test_memory_writability(void);  // Add forward declaration

// Updated memory start address to 0x40000000 for better reliability
#define MEMORY_START  0x40000000  // Using higher memory region known to be writable
#define MEMORY_END    0x48000000  // 128MB region
#define PAGE_SIZE     4096        // 4KB pages

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
    if (page_idx < 8 || page_idx > (0x40000000 / PAGE_SIZE) - 8) {
        debug_hex64("set_bit_addr", addr);
        debug_hex64("set_bit_page", page_idx);
        debug_hex64("set_bit_byte", byte_idx);
        debug_hex64("set_bit_bit", bit_idx);
        debug_hex64("set_bit_val", used);
    }
    
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

// Barebones ASM function that returns
// Note: 'naked' attribute may be ignored by some compilers, but the inline assembly still works
__attribute__((naked)) void test_return(void) {
    asm volatile(
        "mov x9, #0x09000000\n"    // UART address in x9
        "mov w10, #65\n"           // ASCII 'A'
        "str w10, [x9]\n"          // Write 'A' to UART
        "ret\n"                    // Explicit return instruction
    );
    // No C code here - naked function
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
    debug_hex64("page_bitmap @", (uintptr_t)page_bitmap);
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
    debug_hex64("reserved_pages", reserved_pages);
    
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
            debug_hex64("[PMM] alloc_page -> ", addr);
            debug_hex64("[PMM] alloc #", pmm_stats.total_allocations);

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

