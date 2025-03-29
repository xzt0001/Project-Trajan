#include "../include/types.h"
#include "../include/pmm.h"
#include "../include/uart.h"
#include "../include/string.h"

// TODO: Adjust these values based on actual memory map
#define MEMORY_START  0x80000     // Kernel load address
#define MEMORY_END    0x4000000   // 64MB for QEMU
#define PAGE_SIZE     4096        // 4KB pages

// Kernel size is determined by linker script
extern char __kernel_end;

// Static bitmap for page allocation
// Each bit represents one 4KB page (1 = used, 0 = free)
#define BITMAP_SIZE ((MEMORY_END - MEMORY_START) / PAGE_SIZE / 8)
static uint8_t page_bitmap[BITMAP_SIZE];

// Mark a page as used or free
static void set_page_bit(uint64_t addr, int used) {
    if (addr < MEMORY_START || addr >= MEMORY_END) {
        return;  // Address out of range
    }
    
    uint64_t page_idx = (addr - MEMORY_START) / PAGE_SIZE;
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    
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
    /* if-else statement version: 
    if (page_bitmap[byte_idx] & (1 << bit_idx)) {
    return 1; // Page is used
        } else {
    return 0; // Page is free
    }
    */
}

void init_pmm(void) {
    // Clear bitmap first
    memset(page_bitmap, 0, BITMAP_SIZE);
    
    // Mark kernel pages as used (up to 0x40000)
    for (uint64_t addr = MEMORY_START; addr < 0x40000; addr += PAGE_SIZE) {
        set_page_bit(addr, 1);
    }
    
    // No output here - will be handled in main.c
}

void* alloc_page(void) {
    // Find first free page
    for (uint64_t addr = MEMORY_START; addr < MEMORY_END; addr += PAGE_SIZE) {
        if (!is_page_used(addr)) {
            set_page_bit(addr, 1);  // Mark as used
            void* page = (void*)addr;
            memset(page, 0, PAGE_SIZE);  // Zero the page
            return page;
        }
    }
    
    uart_puts("[PMM] ERROR: Out of memory!\n");
    return NULL;  // No free pages
}

void free_page(void* addr) {
    set_page_bit((uint64_t)addr, 0);  // Mark as free
}

// Reserve a specific number of pages for page tables
void reserve_pages_for_page_tables(uint64_t num_pages) {
    uint64_t reserved = 0;
    uint64_t kernel_end = 0x40000; // Use fixed value matching our output
    
    // Start looking right after the kernel
    for (uint64_t addr = kernel_end; addr < MEMORY_END && reserved < num_pages; addr += PAGE_SIZE) {
        if (!is_page_used(addr)) {
            set_page_bit(addr, 1); // Mark as used
            reserved++;
        }
    }
}

