@ rpi4 board hooks for the shared boot path (arch/arm/boot/_start.S).
@ These override the weak defaults there.

#include "arch/arm/rpi4/boot.h"

.section .text.boot, "ax"

@ BCM2711 in low-peripheral mode
.equ GPIO_BASE,         0xFE200000
.equ AUX_ENABLES,       0xFE215004
.equ GPFSEL1_OFF,       0x04
.equ GPIO_PUP_PDN0_OFF, 0xE4

.equ UART_DR,    0x00
.equ UART_FR,    0x18

/* Route GPIO14/15 to ALT0 (TXD0/RXD0), drop their pulls and keep the
 * mini-UART off, so the PL011 actually reaches the header pins. Called by
 * early_uart_init before it programs the PL011. Clobbers r0-r1. */
.global board_console_pinmux
.type board_console_pinmux, %function
board_console_pinmux:
    /* GPIO14/15 -> ALT0 (FSEL fields 0b100) */
    ldr     r1, =GPIO_BASE
    ldr     r0, [r1, #GPFSEL1_OFF]
    bic     r0, r0, #(0x3F << 12)   @ clear FSEL14 [14:12], FSEL15 [17:15]
    orr     r0, r0, #(0x4 << 12)    @ FSEL14 = ALT0
    orr     r0, r0, #(0x4 << 15)    @ FSEL15 = ALT0
    str     r0, [r1, #GPFSEL1_OFF]

    /* No pull on GPIO14/15 (BCM2711 direct pull registers, not the old
     * GPPUD/GPPUDCLK sequence) */
    ldr     r0, [r1, #GPIO_PUP_PDN0_OFF]
    bic     r0, r0, #(0xF << 28)    @ GPIO14 [29:28] = 00, GPIO15 [31:30] = 00
    str     r0, [r1, #GPIO_PUP_PDN0_OFF]

    /* Belt and braces: make sure the mini-UART is not driving anything */
    ldr     r1, =AUX_ENABLES
    ldr     r0, [r1]
    bic     r0, r0, #1
    str     r0, [r1]
    bx      lr
.size board_console_pinmux, . - board_console_pinmux


/* The Pi firmware enters 32-bit kernels in HYP. Install canary vectors before
 * dropping to SVC so anything that later traps to HYP says so, instead of
 * hanging silently. Clobbers r1. */
.global board_hyp_setup
.type board_hyp_setup, %function
board_hyp_setup:
    ldr     r1, =hyp_vectors
    mcr     p15, 4, r1, c12, c0, 0  @ HVBAR
    isb
    bx      lr
.size board_hyp_setup, . - board_hyp_setup


/* HYP canary vector table (see HVBAR install in _start). Each entry floods
 * the UART with a letter identifying how HYP was entered:
 *   U undef, H hvc, P prefetch abort, D data abort, T trapped guest op,
 *   I irq, F fiq, R reset slot (never used).
 * Runs with the HYP MMU off: physical addresses only, no stack. */
.balign 32
hyp_vectors:
    b       hyp_die_reset           @ 0x00 (unused)
    b       hyp_die_undef           @ 0x04 undefined instruction
    b       hyp_die_hvc             @ 0x08 hypervisor call
    b       hyp_die_pabt            @ 0x0C prefetch abort
    b       hyp_die_dabt            @ 0x10 data abort
    b       hyp_die_trap            @ 0x14 trapped guest operation
    b       hyp_die_irq             @ 0x18 IRQ
    b       hyp_die_fiq             @ 0x1C FIQ

hyp_die_reset:
    mov     r0, #'R'
    b       hyp_die
hyp_die_undef:
    mov     r0, #'U'
    b       hyp_die
hyp_die_hvc:
    mov     r0, #'H'
    b       hyp_die
hyp_die_pabt:
    mov     r0, #'P'
    b       hyp_die
hyp_die_dabt:
    mov     r0, #'D'
    b       hyp_die
hyp_die_trap:
    mov     r0, #'T'
    b       hyp_die
hyp_die_irq:
    mov     r0, #'I'
    b       hyp_die
hyp_die_fiq:
    mov     r0, #'F'
    b       hyp_die

hyp_die:
    ldr     r1, =BOARD_UART0_BASE
1:
    ldr     r2, [r1, #UART_FR]
    tst     r2, #(1 << 5)           @ TXFF
    bne     1b
    str     r0, [r1, #UART_DR]
    ldr     r2, =0x2000000          @ crude pacing so the flood is readable
2:
    subs    r2, r2, #1
    bne     2b
    b       1b
.size hyp_vectors, . - hyp_vectors
