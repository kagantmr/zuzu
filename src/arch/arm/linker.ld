ENTRY(_start)

SECTIONS {
    . = 0x80000000;
    _kernel_start = .;

    .text : { KEEP(*(.text.boot)) *(.text*) }
    .rodata : { *(.rodata*) }

    .data : { _data_start = .; *(.data*); _data_end = .; }

    .bss : ALIGN(8) { _bss_start = .; *(.bss*); *(COMMON); _bss_end = .; }

    _kernel_end = .;

    /* Bitmap just after kernel, page aligned */
    __bitmap_start__ = ALIGN(_kernel_end, 0x1000);
    . = __bitmap_start__ + 0x1000;  /* adjust size as needed */
    __bitmap_end__ = .;

    /* Stack at the top of RAM */
    __stack_base__ = 0x80800000;    /* end of RAM */
    __stack_top__  = __stack_base__ - 0x1000; /* 4 KB stack */
}