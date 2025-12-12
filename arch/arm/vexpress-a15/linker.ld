ENTRY(_start)

SECTIONS {
    __dtb_addr__ = 0x80000000;  /* Address where DTB is loaded by bootloader */
    . = 0x80010000;
    _kernel_start = .;

    /* Put boot code first */
    .text : {
        KEEP(*(.text.boot))   /*  _start lives here */
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

    /* Hardcode stack to 0x80800000 (Safety Gap) */
    __stack_base__ = 0x80800000;
    __stack_top__  = __stack_base__ - 0x1000;
}