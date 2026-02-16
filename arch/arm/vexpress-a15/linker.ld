ENTRY(_start)

/* =============================================================================
 * Higher-Half Kernel Memory Layout Constants
 * ============================================================================= */
KERNEL_PA_BASE = 0x80000000;        /* Physical RAM base */
KERNEL_VA_BASE = 0xC0000000;        /* Virtual kernel base (higher-half) */
BOOT_OFFSET    = 0x10000;           /* Offset from base (64KB for DTB space) */

/* Boot physical address */
BOOT_PA        = KERNEL_PA_BASE + BOOT_OFFSET;     /* 0x80010000 */

PHDRS
{
    boot    PT_LOAD FLAGS(1 | 4);   /* PF_X | PF_R - boot code at physical */
    text    PT_LOAD FLAGS(1 | 4);   /* PF_X | PF_R */
    rodata  PT_LOAD FLAGS(4);       /* PF_R */
    data    PT_LOAD FLAGS(2 | 4);   /* PF_W | PF_R */
}

SECTIONS
{
    /* DTB physical address (provided by bootloader at RAM base) */
    __dtb_addr__ = KERNEL_PA_BASE;  /* 0x80000000 */

    /* =========================================================================
     * Boot Section - Runs at PHYSICAL addresses before MMU is enabled
     * VMA = LMA = 0x80010000
     * ========================================================================= */
    . = BOOT_PA;
    _boot_start = .;

    .text.boot : {
        /* _start MUST come first - it's the entry point */
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

    /* =========================================================================
     * Main Kernel - Linked at VIRTUAL addresses
     * VMA is computed dynamically: VA = VA_BASE + (PA - PA_BASE)
     * This ensures the MMU mapping VA_BASE+X -> PA_BASE+X works correctly
     * ========================================================================= */
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

    /* =========================================================================
     * Physical address helpers for boot code
     * ========================================================================= */
    _kernel_phys_start = _boot_end;
    _kernel_phys_end   = _kernel_phys_start + (_kernel_end - _kernel_start);

    /* =========================================================================
     * Stack Allocation Region (physical addresses for now)
     * These will need virtualization later
     * ========================================================================= */
    . = ALIGN(16);

    /* Stack base (top of low RAM area) */
    __stack_region_base__ = 0x80800000;

    /* Per-mode stack sizes */
    __svc_stack_size__   = 0x2000;   /* 8 KB */
    __irq_stack_size__   = 0x400;    /* 1 KB */
    __abt_stack_size__   = 0x400;    /* 1 KB */
    __und_stack_size__   = 0x400;    /* 1 KB */

    /* SVC stack */
    __svc_stack_top__  = __stack_region_base__;
    __svc_stack_base__ = __svc_stack_top__ - __svc_stack_size__;

    /* IRQ stack */
    __irq_stack_top__  = __svc_stack_base__;
    __irq_stack_base__ = __irq_stack_top__ - __irq_stack_size__;

    /* Abort stack */
    __abt_stack_top__  = __irq_stack_base__;
    __abt_stack_base__ = __abt_stack_top__ - __abt_stack_size__;

    /* Undefined stack */
    __und_stack_top__  = __abt_stack_base__;
    __und_stack_base__ = __und_stack_top__ - __und_stack_size__;

    /* Bottom of all stacks (for sanity checks) */
    __stack_region_end__ = __und_stack_base__;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note.*)
    }
}
