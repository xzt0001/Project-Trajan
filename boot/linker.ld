/* Linker script for our kernel */
ENTRY(_start)

/* Define memory sections with attributes */
PHDRS
{
  text PT_LOAD FLAGS(5);    /* 5 = RX (read + execute) */
  data PT_LOAD FLAGS(6);    /* 6 = RW (read + write) */
}

SECTIONS {
    /* Start address in QEMU */
    . = 0x80000;
    
    __text_start = .;  /* Mark start of text section for debugging */

    /* Boot section containing _start and kernel_main (force these to be together) */
    .text.boot : {
        *(.text.boot)      /* Start with boot code */
        *(.text.boot.main) /* Force kernel_main to be placed here */
    } :text
    
    /* Regular code section - ensure 8-byte alignment for all functions */
    .text : ALIGN(8) { 
        *(.text)           /* Regular text section */
    } :text
    
    /* Other text sections (except vectors) */
    .text.rest : {
        *(.text.*)
        *(.text.*)         /* Any remaining text subsections */
        /* Exclude vectors section which needs special alignment */
        EXCLUDE_FILE(*vector.o) *(.text.*)
    } :text
    
    /* Vector table at fixed address as needed by our fix */
    . = 0x1000000;
    .vector_table ALIGN(0x800) : {
        KEEP(*(.text.vectors))
    } :text
    
    __text_end = .;  /* Mark end of text section for debugging */

    /* Read-only data */
    .rodata : { 
        *(.rodata) 
        *(.rodata.*)
    } :text

    /* Read-write data (initialized) */
    .data : { 
        *(.data) 
        *(.data.*)
    } :data

    /* Uninitialized data + stack */
    . = ALIGN(4096);  /* Align to 4KB boundary */
    .bss : {
        _bss_start = .; 
        *(.bss*) 
        *(COMMON) 
        _bss_end = .;
    } :data
    
    . = ALIGN(4096);  /* Ensure end is also 4KB aligned */
    __kernel_end = .;  /* This will be used as reference for page_bitmap placement */
}
