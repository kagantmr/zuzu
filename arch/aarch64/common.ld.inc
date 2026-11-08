/* Shared kernel linker-script body for AArch64 boards; each board's linker.ld defines
 * KERNEL_PA_BASE and BOOT_OFFSET before INCLUDE'ing it. */

KERNEL_VA_BASE = 0xFFFF000000000000;        /* Virtual kernel base (higher-half) */

/* Boot physical address. The image is objcopy'd to a flat binary whose
 * first byte must be _start (the firmware/QEMU jumps straight to the load
 * address), so _start.S keeps _start at the top of .text.boot. */
BOOT_PA        = KERNEL_PA_BASE + BOOT_OFFSET;

PHDRS
{
    boot    PT_LOAD FLAGS(1 | 4);   /* PF_X | PF_R - boot code at physical */
    text    PT_LOAD FLAGS(1 | 4);   /* PF_X | PF_R */
    rodata  PT_LOAD FLAGS(4);       /* PF_R */
    data    PT_LOAD FLAGS(2 | 4);   /* PF_W | PF_R */
}

SECTIONS
{
    . = BOOT_PA;
    _boot_start = .;

    .text.boot : {
        KEEP(*_start.o(.text.boot))
        KEEP(*_start.o(.text.boot.*))
        /* Then other boot code */
        *(.text.boot)
        *(.text.boot.*)
    } : boot

    . = ALIGN(4096);
    .bss.boot (NOLOAD) : {
        _early_bss_start = .;
        *(.bss.boot)
        *(.bss.boot.*)
        _early_bss_end = .;
    } : boot

    . = ALIGN(4096);
    _boot_end = .;

    . = KERNEL_VA_BASE + (_boot_end - KERNEL_PA_BASE);
    _kernel_start = .;

    /* --- Code Section --- */
    .text : AT(_boot_end) {
        *(.vectors)                 /* Exception vectors first */
        *(.text)
        *(.text.*)
    } : text

    . = ALIGN(4096);
    _rodata_start = .;

    /* --- Read-only Data --- */
    /* LMA must match VMA offset: if VMA is aligned, LMA must be too */
    .rodata : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 4096)) {
        /* Must precede the wildcards below, and must not be named
         * .rodata.* or *(.rodata.*) would swallow it. KEEP is required:
         * the bracket symbols are the only referrers. */
        . = ALIGN(4);
        __zuzu_drivers_start = .;
        KEEP(*(SORT_BY_NAME(.zuzu_drivers.*)))
        __zuzu_drivers_end = .;
        *(.rodata)
        *(.rodata.*)
    } : rodata

    . = ALIGN(4096);

    /* --- Initialized Data --- */
    .data : AT(ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 4096)) {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } : data

    . = ALIGN(8);

    /* --- Uninitialized Data (BSS) --- */
    .bss (NOLOAD) : ALIGN(8) {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } : data

    . = ALIGN(4096);
    _kernel_end = .;

    _kernel_phys_start = _boot_end;
    _kernel_phys_end   = _kernel_phys_start + (_kernel_end - _kernel_start);

    . = ALIGN(16);


    __kstack_size__    = 0x2000;
    __kstack_phys_top  = KERNEL_PA_BASE + 0x00800000;
    __kstack_phys_base = __kstack_phys_top - __kstack_size__;

    __kstack_base  = __kstack_phys_base + (KERNEL_VA_BASE - KERNEL_PA_BASE);
    __kstack_top   = __kstack_phys_top  + (KERNEL_VA_BASE - KERNEL_PA_BASE);
    _boot_start_va = _boot_start        + (KERNEL_VA_BASE - KERNEL_PA_BASE);
    _boot_end_va   = _boot_end          + (KERNEL_VA_BASE - KERNEL_PA_BASE);
    
    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
