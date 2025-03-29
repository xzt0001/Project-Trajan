/* Linker script for our kernel */
ENTRY(_start)

SECTIONS {
    /* Start address in QEMU */
    . = 0x80000;

    /* Code section */
    .text.boot : { *(.text.boot) }
    .text : { *(.text) }

    /* Read-only data */
    .rodata : { *(.rodata) }

    /* Read-write data (initialized) */
    .data : { *(.data) }

    /* Uninitialized data + stack */
    . = ALIGN(4096);  /* Align to 4KB boundary */
    .bss : {
        _bss_start = .; 
        *(.bss*) 
        *(COMMON) 
        _bss_end = .;
    }
    . = ALIGN(4096);  /* Ensure end is also 4KB aligned */
    __kernel_end = .;
}
