ENTRY(_start)

USER_BASE = 0x10000;

/* Newlib lite-exit only registers __libc_fini_array (the .fini_array walk)
 * via atexit when the weak symbol __libc_fini is defined. Point it at _fini
 * so tier-2 destructors run; tier-1 never references it. */
PROVIDE(__libc_fini = _fini);

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R | PF_X */
    data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    . = USER_BASE;

    .text : {
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } :text

    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.data.*)
    } :data

    . = ALIGN(4);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } :data

    . = ALIGN(4);
    _end = .;

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note.*)
    }
}