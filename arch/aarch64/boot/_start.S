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
    mrs x0, MPIDR_EL1
    and x0, x0, #0xff
    cbz x0, 1f
0:
    wfe
    b       0b
1:
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #1 // SVC is EL1 in AArch64, if we're alr EL1 skip the hypervisor mess
    b.eq    el1_entry
    cmp     x0, #2 // HYP is EL2
    b.eq    from_el2
    b       0b     // park, we didn't expect to land in this mode


from_el2:
    // we run AArch64, so set HCR's MSB to indicate that
    mov     x0, #1
    lsl     x0, x0, #31
    msr     HCR_EL2, x0


    // CNTHCTL_EL2 decides whether EL1/EL0 may touch the physical counter and timer.
    mov     x0, #3
    msr     CNTHCTL_EL2, x0

    // hypervisor timer offset, clear it
    msr     CNTVOFF_EL2, xzr

    // el1 can use fpu, simd, sve
    ldr x0, =0x33FF
    msr CPTR_EL2, x0

    // d, a, i, f masked, EL1, use SP_EL1
    ldr x0, =0x3C5
    msr SPSR_EL2, x0


    ldr x0, =el1_entry
    msr ELR_EL2, x0

    isb  // enforce the changes went through, even though eret probably does this on its own we must be safe
    eret

el1_entry:
    ldr     x0, =0x30D00800
    msr     SCTLR_EL1, x0
    isb

    ldr x0, =__svc_stack_top__
    mov sp, x0

    bl      early_uart_init

    ldr     w0, [x19]
    ldr     w1, =0xedfe0dd0
    cmp     w0, w1
    b.ne    dtb_panic
mmu_setup:
    ldr x0, =boot_l0_low
    ldr x1, =boot_l1 + 4096
1:
    stp xzr, xzr, [x0], #16
    cmp x0, x1
    b.lo 1b

    ldr x0, =boot_l1
    orr x0, x0, #0b11

    ldr x1, =boot_l0_low
    ldr x2, =boot_l0_high

    str x0, [x1]
    str x0, [x2]

    ldr x3, =boot_l1
    ldr x4, =0x705
    str x4, [x3]              // entry 0

    ldr x4, =0x00600000C0000401 // entry 3
    str x4, [x3, #24]

    ldr x3, =0xFF00 // device, normal writeback
    msr MAIR_EL1, x3 

    ldr x3, =0x2B5103510 // T0SZ=16 -> 0x10; walk cacheability and shareability for TTBR0 -> 0x3500; T1SZ=16 -> 16<<16 = 0x100000; TTBR1 cacheability, shareability and TG1=2 -> 0xB5000000; IPS=2 -> 2<<32.
    msr TCR_EL1, x3

    msr TTBR0_EL1, x1
    msr TTBR1_EL1, x2

    isb
    tlbi vmalle1
    dsb ish
    isb

    mrs x0, SCTLR_EL1
    ldr x1, =0x1005 // mmu on, dcache on, icache on
    orr x0, x0, x1
    msr SCTLR_EL1, x0
    isb

    ldr x0, =higher_half_entry
    br  x0
hang:
    wfi
    b hang
    

.size _start, . - _start

/** Board hook that routes the UART pins before early_uart_init; does nothing by default. */
.weak board_console_pinmux
.type board_console_pinmux, %function
board_console_pinmux:
    ret
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
 * Clobbers: x0, x1.
 */
.type early_uart_init, %function
early_uart_init:
    stp x29, x30, [sp, #-16]!
    bl      board_console_pinmux
    ldp x29, x30, [sp], #16

    ldr     x1, =BOARD_UART0_BASE
    mov     w0, #0
    str     w0, [x1, #UART_CR]
    ldr     w0, =0x7FF
    str     w0, [x1, #UART_ICR]
    mov     w0, #BOARD_UART_IBRD
    str     w0, [x1, #UART_IBRD]
    mov     w0, #BOARD_UART_FBRD
    str     w0, [x1, #UART_FBRD]
    mov     w0, #0x70
    str     w0, [x1, #UART_LCRH]
    ldr     w0, =0x301
    str     w0, [x1, #UART_CR]
    ret
.size early_uart_init, . - early_uart_init

/**
 * early_uart_putc
 *
 * Writes one byte once the transmit FIFO has room.
 * In:       x0 = byte.
 * Clobbers: x1, x2.
 */
.global early_uart_putc
.type early_uart_putc, %function
early_uart_putc:
    ldr     x1, =BOARD_UART0_BASE
1:
    ldr     w2, [x1, #UART_FR]
    tst     w2, #(1 << 5)
    b.ne     1b
    str     w0, [x1, #UART_DR]
    ret
.size early_uart_putc, . - early_uart_putc

/**
 * early_uart_puthex
 *
 * Prints x0 as 16 hex digits, most significant first.
 * In:       x0 = value.
 * Clobbers: x0-x2.
 */
.global early_uart_puthex
.type early_uart_puthex, %function
early_uart_puthex:
    stp     x29, x30, [sp, #-32]!
    stp     x20, x21, [sp, #16]
    mov     x20, x0
    mov     w21, #16
1:
    ror     x20, x20, #60
    and     x0, x20, #0xf
    add     x0, x0, #'0'
    cmp     x0, #'9'
    b.ls    2f
    add     x0, x0, #('a' - '9' - 1)
2:
    bl      early_uart_putc
    subs    w21, w21, #1
    b.ne    1b
    ldp     x20, x21, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
.size early_uart_puthex, . - early_uart_puthex

/** dtb_panic: prints dtb_panic_msg and halts. Never returns. */
.type dtb_panic, %function
dtb_panic:
    ldr     x20, =dtb_panic_msg
1:
    ldrb    w21, [x20], #1
    cbz     w21, 2f
    mov     w0, w21
    bl      early_uart_putc
    b       1b
2:
    wfe
    b       2b
.size dtb_panic, . - dtb_panic

dtb_panic_msg:
    .asciz "\r\nPANIC: x0 is not a DTB (missing -dtb?)\r\n"
.balign 4

.section .text
.balign 4096
higher_half_entry:

    ldr x0, =vector_table
    msr VBAR_EL1, x0
    isb

    ldr x0, =__svc_stack_top__
    ldr x1, =KERNEL_VA_BASE
    add x0, x0, x1
    mov sp, x0

    mov x0, x19
    bl early

2:
    wfi
    b 2b

.section .bss.boot, "aw", %nobits
.balign 4096
boot_l0_low:  .skip 4096
boot_l0_high: .skip 4096
boot_l1:      .skip 4096
