ENTRY(_start)

SECTIONS {
    . = 0x80000000;           /* Load address for vexpress-a15 */
    _kernel_start = .;
    
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    _kernel_end = .;
}