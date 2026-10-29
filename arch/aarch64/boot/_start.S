/**
 * @file
 * @brief AArch64 kernel entry
 */

.section .text.boot, "ax"

#include BOARD_BOOT_H

/**
 * _start
 *
 * Entered in physical memory, x0 = DTB address, and the code will leave EL2.
 */
.global _start
.type _start, %function

_start:
    msr daifset, #0xf

    mov x19, x0 // saving the DTB pointer before anything

    // park if core id != 0
    mrs MPIDR_EL1, x0 
    and x0, x0, #0xFF
    beq     1f
0:
    wfe
    b       0b
1:
    mrs     r0, cpsr
    and     r1, r0, #0x1F
    cmp     r1, #MODE_HYP
    bne     svc_entry

    mrc     p15, 4, r1, c14, c1, 0
    orr     r1, r1, #0x3
    mcr     p15, 4, r1, c14, c1, 0
    mov     r1, #0
    mcrr    p15, 4, r1, r1, c14

    mcr     p15, 4, r1, c1, c1, 0
    mcr     p15, 4, r1, c1, c1, 1
    mcr     p15, 4, r1, c1, c1, 2
    mcr     p15, 4, r1, c1, c1, 3

    bic     r0, r0, #0x1F
    orr     r0, r0, #(MODE_SVC | 0xC0)
    msr     spsr_cxsf, r0
    ldr     r1, =svc_entry
    msr     elr_hyp, r1
    eret

svc_entry:

    mov     r4, r2

    bl      early_uart_init

    mov     r0, #'z'
    bl      early_uart_putc
    mov     r0, #'u'
    bl      early_uart_putc

    ldr     r0, [r4]
    ldr     r1, =0xedfe0dd0
    cmp     r0, r1
    beq     1f
    bl      dtb_panic
1:

.size _start, . - _start

/** Board hook that routes the UART pins before early_uart_init; does nothing by default. */
.weak board_console_pinmux
.type board_console_pinmux, %function
board_console_pinmux:
    bx      lr
.size board_console_pinmux, . - board_console_pinmux

/** PL011 register offsets. */
.equ UART_DR,    0x00
.equ UART_FR,    0x18
.equ UART_IBRD,  0x24
.equ UART_FBRD,  0x28
.equ UART_LCRH,  0x2C
.equ UART_CR,    0x30
.equ UART_ICR,   0x44

/**
 * early_uart_init
 *
 * Runs board_console_pinmux, then sets the PL011 to the board's baud divisors,
 * 8 bits with FIFOs, and enables transmit and receive.
 * Clobbers: r0, r1.
 */
.type early_uart_init, %function
early_uart_init:
    push    {lr}
    bl      board_console_pinmux
    pop     {lr}
    ldr     r1, =BOARD_UART0_BASE
    mov     r0, #0
    str     r0, [r1, #UART_CR]
    ldr     r0, =0x7FF
    str     r0, [r1, #UART_ICR]
    mov     r0, #BOARD_UART_IBRD
    str     r0, [r1, #UART_IBRD]
    mov     r0, #BOARD_UART_FBRD
    str     r0, [r1, #UART_FBRD]
    mov     r0, #0x70
    str     r0, [r1, #UART_LCRH]
    ldr     r0, =0x301
    str     r0, [r1, #UART_CR]
    bx      lr
.size early_uart_init, . - early_uart_init

/**
 * early_uart_putc
 *
 * Writes one byte once the transmit FIFO has room.
 * In:       r0 = byte.
 * Clobbers: r1, r2.
 */
.global early_uart_putc
.type early_uart_putc, %function
early_uart_putc:
    ldr     r1, =BOARD_UART0_BASE
1:
    ldr     r2, [r1, #UART_FR]
    tst     r2, #(1 << 5)
    bne     1b
    str     r0, [r1, #UART_DR]
    bx      lr
.size early_uart_putc, . - early_uart_putc

/** dtb_panic: prints dtb_panic_msg and halts. Never returns. */
.type dtb_panic, %function
dtb_panic:
    ldr     r5, =dtb_panic_msg
1:
    ldrb    r6, [r5], #1
    cmp     r6, #0
    beq     2f
    mov     r0, r6
    bl      early_uart_putc
    b       1b
2:
    wfe
    b       2b
.size dtb_panic, . - dtb_panic

dtb_panic_msg:
    .asciz "\r\nPANIC: x0 is not a DTB (missing -dtb?)\r\n"
.balign 4
