ENTRY(_start)

SECTIONS {
    . = 0x80000000;
    _kernel_start = .;

    /* Put boot code first */
    .text : {
        KEEP(*(.text.boot))   /* our _start lives here */
        *(.text*)
    }

    .rodata : { *(.rodata*) }

    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    .bss : ALIGN(8) {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    _kernel_end = .;

    . = ALIGN(8);
    __stack_top__ = .;
    . = . + 0x4000;  /* 16 KB stack is enough for now */
    __stack_base__ = .;
}