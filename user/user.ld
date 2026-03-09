ENTRY(_start)

USER_BASE = 0x10000;

SECTIONS
{
    . = USER_BASE;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    }

    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(4);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    . = ALIGN(4);
    _end = .;

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note.*)
    }
}