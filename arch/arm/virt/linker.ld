ENTRY(_start)

KERNEL_PA_BASE = 0x40000000;        /* Physical RAM base (QEMU `virt`) */
KERNEL_VA_BASE = 0xC0000000;        /* Virtual kernel base (higher-half) */
BOOT_OFFSET    = 0x10000;           /* QEMU's raw-kernel load offset for virt */

/* Boot physical address. The image is objcopy'd to a flat binary and
 * entered directly by QEMU (no firmware on this platform), same as rpi4. */
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

    . = ALIGN(16384);               /* L1 table must be 16KB aligned */
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

    /* --- Read-only Data --- */
    /* LMA must match VMA offset: if VMA is aligned, LMA must be too */
    .rodata : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 4096)) {
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
        _bss_end = .;
    } : data

    . = ALIGN(4096);
    _kernel_end = .;

    _kernel_phys_start = _boot_end;
    _kernel_phys_end   = _kernel_phys_start + (_kernel_end - _kernel_start);

    . = ALIGN(16);


    /* Per-mode stack sizes */
    __svc_stack_size__   = 0x2000;   /* 8 KB */
    __irq_stack_size__   = 0x400;    /* 1 KB */
    __abt_stack_size__   = 0x400;    /* 1 KB */
    __und_stack_size__   = 0x400;    /* 1 KB */

    /* Top of the 8 MB bootstrap-mapped region (see _start.S) */
    __stack_region_end__ = KERNEL_PA_BASE + 0x00800000;

    __svc_stack_top__  = __stack_region_end__;
    __svc_stack_base__ = __svc_stack_top__ - __svc_stack_size__;

    __irq_stack_top__  = __svc_stack_base__;
    __irq_stack_base__ = __irq_stack_top__ - __irq_stack_size__;

    __abt_stack_top__  = __irq_stack_base__;
    __abt_stack_base__ = __abt_stack_top__ - __abt_stack_size__;

    __und_stack_top__  = __abt_stack_base__;
    __und_stack_base__ = __und_stack_top__ - __und_stack_size__;

    __stack_region_base__ = __und_stack_base__;    /* low end, for pmm_mark_range */

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note.*)
    }
}
