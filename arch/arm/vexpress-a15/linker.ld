ENTRY(_start)

SECTIONS
{
    __dtb_addr__ = 0x80000000;   /* DTB provided by bootloader */

    . = 0x80010000;
    _kernel_start = .;

    /* --- Code --- */
    .text : {
        KEEP(*(.text.boot))     /* _start */
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

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

    /* --------------------------------------------------
       Stack allocation region (high memory)
       -------------------------------------------------- */

    . = ALIGN(16);

    /* Stack base (top of RAM area you chose) */
    __stack_region_base__ = 0x80800000;

    /* Per-mode stack sizes */
    __svc_stack_size__   = 0x4000;  /* 16 KB */
    __irq_stack_size__   = 0x2000;  /* 8 KB */
    __abt_stack_size__   = 0x2000;  /* 8 KB */
    __und_stack_size__   = 0x2000;  /* 8 KB */

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

    /* Bottom of all stacks (for sanity checks later) */
    __stack_region_end__ = __und_stack_base__;
}