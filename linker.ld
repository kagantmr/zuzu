ENTRY(_start)

SECTIONS {
    . = 0x80000000;           /* Load address for vexpress-a15 */
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    . = ALIGN(8);
    STACK_BASE = .;
    . += 0x10000;
    STACK_TOP = .;
}