/**
 * @file
 * @brief AArch64 exception vector table; VBAR points here once the kernel runs in the higher half.
 */

/** 32-byte aligned, as VBAR requires. One branch per exception, in hardware order. */
.section .vectors, "ax"
.align 11
.global vector_table

.macro ventry label
    .balign 0x80
    b \label
.endm

vector_table:
    ventry unhandled    // 0x000  cur EL, SP_EL0, sync
    ventry unhandled    // 0x080  cur EL, SP_EL0, IRQ
    ventry unhandled    // 0x100  cur EL, SP_EL0, FIQ
    ventry unhandled    // 0x180  cur EL, SP_EL0, SError

    .balign 0x80
    ventry unhandled    // 0x000  cur EL, SP_EL0, sync
    ventry unhandled    // 0x080  cur EL, SP_EL0, IRQ
    ventry unhandled    // 0x100  cur EL, SP_EL0, FIQ
    ventry unhandled    // 0x180  cur EL, SP_EL0, SError

    .balign 0x80
    ventry unhandled    // 0x000  cur EL, SP_EL0, sync
    ventry unhandled    // 0x080  cur EL, SP_EL0, IRQ
    ventry unhandled    // 0x100  cur EL, SP_EL0, FIQ
    ventry unhandled    // 0x180  cur EL, SP_EL0, SError

    .balign 0x80
    ventry unhandled    // 0x000  cur EL, SP_EL0, sync
    ventry unhandled    // 0x080  cur EL, SP_EL0, IRQ
    ventry unhandled    // 0x100  cur EL, SP_EL0, FIQ
    ventry unhandled    // 0x180  cur EL, SP_EL0, SError

unhandled:
    mrs x0, ESR_EL1
    bl early_uart_puthex

    mov x0, '\n'
    bl early_uart_putc

    mrs x0, ELR_EL1
    bl early_uart_puthex
    
    mov x0, '\n'
    bl early_uart_putc

    mrs x0, FAR_EL1
    bl early_uart_puthex
    
    mov x0, '\n'
    bl early_uart_putc
    
dead:
    b dead
