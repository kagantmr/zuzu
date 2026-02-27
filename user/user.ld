ENTRY(_start)

USER_BASE = 0x10000;

SECTIONS
{
    . = USER_BASE;

    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(4);

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4);

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