/* Linker script for our kernel */
ENTRY(_start)

/* Define memory sections with attributes */
PHDRS
{
  text PT_LOAD FLAGS(5);    /* 5 = RX (read + execute) */
  data PT_LOAD FLAGS(6);    /* 6 = RW (read + write) */
  vectors PT_LOAD FLAGS(5); /* Separate segment for vector table (RX) */
  rodata PT_LOAD FLAGS(4);  /* 4 = R (read-only) */
}

SECTIONS {
    /* Start address in QEMU */
    . = 0x80000;
    
    /* Text section with exported symbols for MMU mapping */
    __text_start = .;  /* Mark start of text section for MMU mapping */

    /* Boot section containing _start and kernel_main (force these to be together) */
    .text.boot : ALIGN(0x1000) {
        *(.text.boot)      /* Start with boot code */
        *(.text.boot.main) /* Force kernel_main to be placed here */
    } :text
    
    /* Regular code section - ensure 8-byte alignment for all functions */
    .text : ALIGN(0x1000) { 
        *(.text)           /* Regular text section */
        . = ALIGN(2048);   /* 2KB align for trampoline */
        *(.text.tramp)     /* MMU trampoline section */
        . = ALIGN(2048);   /* Maintain alignment after */
    } :text
    
    /* Other text sections (except vectors) */
    .text.rest : {
        /* *(.text.*)          Any remaining text subsections */
        /* Exclude vectors section which needs special alignment */
        EXCLUDE_FILE(*vector.o) *(.text.*)
    } :text
    
    /* Calculate the end address of .text.rest for debugging */
    __text_rest_end = .;
    __text_end = .;  /* Mark end of text section for MMU mapping */
    
    /* Position the vector table load address (LMA) right after the text segment */
    . = ALIGN(4096); /* Ensure 4KB alignment for the next section */
    _vector_table_load_start = .;
    
    /* Virtual memory address for vector table (virtual address after MMU) */
    . = 0x1000000;
    .vector_table ALIGN(0x800) : AT(_vector_table_load_start) {
        vector_table = .;
        *(.text.vectors)
    } :vectors
    
    /* Export the vector table load address for early boot */
    PROVIDE(_vector_table_load_start = LOADADDR(.vector_table));
    
    /* For optional manual references, now dynamic based on text end */
    PROVIDE(vector_table_phys = _vector_table_load_start);
    
    /* Explicitly export destination address */
    PROVIDE(_vector_table_dest_start = _vector_table_load_start);
    
    /* Export the vector table source address */
    PROVIDE(_vector_table_source_start = vector_table);
    
    /* Read-only data - place AFTER the vector table */
    . = ALIGN(4096);  /* Ensure it starts at a fresh 4KB boundary after vector table */
    __rodata_start = .; /* Mark start of rodata for MMU mapping */
    .rodata : { 
        *(.rodata) 
        *(.rodata.*)
    } :rodata
    __rodata_end = .; /* Mark end of rodata for MMU mapping */

    /* Read-write data (initialized) */
    . = ALIGN(4096);  /* Align to 4KB boundary for MMU mapping */
    __data_start = .; /* Mark start of data for MMU mapping */
    .data : { 
        *(.data) 
        *(.data.*)
    } :data
    __data_end = .; /* Mark end of data for MMU mapping */

    /* Uninitialized data + stack */
    . = ALIGN(4096);  /* Align to 4KB boundary for MMU mapping */
    __bss_start = .; /* Mark start of bss for MMU mapping */
    .bss : {
        _bss_start = .; 
        *(.bss*) 
        *(COMMON) 
        _bss_end = .;
    } :data
    __bss_end = .; /* Mark end of bss for MMU mapping */
    
    . = ALIGN(4096);  /* Ensure end is also 4KB aligned */
    __kernel_end = .;  /* This will be used as reference for page_bitmap placement */
}
