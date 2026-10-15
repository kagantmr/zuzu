.section .text.boot, "ax"

.equ MODE_IRQ,        0x12
.equ MODE_ABT,        0x17
.equ MODE_UND,        0x1b
.equ MODE_SVC,        0x13
.equ MODE_HYP,        0x1a
#include <arch_impl/armv7_mmu.h>
#include BOARD_BOOT_H
.equ TTBR0_BOOT_ATTRS, TTBR_WALK_ATTRS

.equ VA_OFFSET_IDX,     (BOARD_KERNEL_VA_OFFSET >> 20)
.equ RAM_LOW_IDX_BASE,  (BOARD_RAM_PA_BASE >> 20)
.equ RAM_HIGH_IDX_BASE, (RAM_LOW_IDX_BASE + VA_OFFSET_IDX)
.equ BOOTSTRAP_RAM_MB,  8

.macro SET_MODE_STACK_PHYS mode, top_sym
    cps     #\mode
    ldr     sp, =\top_sym
.endm

.macro SET_MODE_STACK_VIRT mode, top_sym
    cps     #\mode
    ldr     r0, =\top_sym
    add     sp, r0, r1
.endm

.global _start
.type _start, %function

_start:
    cpsid   if

    mrc     p15, 0, r0, c0, c0, 5
    ands    r0, r0, #0xFF
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

    bl      board_hyp_setup

    bic     r0, r0, #0x1F
    orr     r0, r0, #(MODE_SVC | 0xC0)
    msr     spsr_cxsf, r0
    ldr     r1, =svc_entry
    msr     elr_hyp, r1
    eret

svc_entry:
    
    mov     r4, r2

    bl      setup_mode_stacks

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

    mov     r0, r4
    bl      early_paging_init
    mov     r5, r0

    mov     r0, #0
    mcr     p15, 0, r0, c2, c0, 2
    isb

    orr     r0, r5, #TTBR0_BOOT_ATTRS
    mcr     p15, 0, r0, c2, c0, 0
    isb

    mov     r0, #0x1
    mcr     p15, 0, r0, c3, c0, 0
    isb

    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0
    dsb
    isb

    mcr     p15, 0, r0, c7, c5, 0
    dsb
    isb

    mrc     p15, 0, r0, c1, c0, 0
    orr     r0, r0, #(1 << 0)
    orr     r0, r0, #(1 << 2)
    orr     r0, r0, #(1 << 12)
    bic     r0, r0, #(1 << 28)
    bic     r0, r0, #(1 << 1)
    mcr     p15, 0, r0, c1, c0, 0
    isb
    dsb

    ldr     r0, =higher_half_entry
    bx      r0

.size _start, . - _start

.weak board_hyp_setup
.type board_hyp_setup, %function
board_hyp_setup:
    bx      lr
.size board_hyp_setup, . - board_hyp_setup

.weak board_console_pinmux
.type board_console_pinmux, %function
board_console_pinmux:
    bx      lr
.size board_console_pinmux, . - board_console_pinmux

.global early_paging_init
.type early_paging_init, %function

.equ SECTION_NORMAL_DESC, L1_SECT_BOOT_NORMAL
.equ SECTION_DEVICE_DESC, L1_SECT_BOOT_DEVICE

early_paging_init:
    ldr     r2, =early_l1

    mov     r5, #0
    mov     r6, #0
1:
    cmp     r5, #4096
    bge     2f
    str     r6, [r2, r5, lsl #2]
    add     r5, r5, #1
    b       1b
2:
    ldr     r7, =BOARD_RAM_PA_BASE
    ldr     r8, =SECTION_NORMAL_DESC
    mov     r5, #0
    ldr     r10, =BOOTSTRAP_RAM_MB
3:
    cmp     r5, r10
    bge     4f
    add     r11, r7, r5, lsl #20
    orr     r11, r11, r8
    add     r12, r5, #RAM_LOW_IDX_BASE
    str     r11, [r2, r12, lsl #2]
    add     r12, r5, #RAM_HIGH_IDX_BASE
    str     r11, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       3b
4:
    
    lsr     r11, r0, #20
    mov     r5, #0
5:
    cmp     r5, #2
    bge     6f
    add     r12, r11, r5
    lsl     r6, r12, #20
    orr     r6, r6, r8
    str     r6, [r2, r12, lsl #2]
    add     r12, r12, #VA_OFFSET_IDX
    bic     r12, r12, #0x1000
    str     r6, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       5b
6:
    
    ldr     r7, =BOARD_PERIPH_PA_BASE
    ldr     r8, =SECTION_DEVICE_DESC
    mov     r5, #0
    ldr     r10, =BOARD_PERIPH_MB
7:
    cmp     r5, r10
    bge     8f
    add     r11, r7, r5, lsl #20
    orr     r11, r11, r8
    lsr     r12, r11, #20
    str     r11, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       7b
8:
    mov     r0, r2
    bx      lr
.size early_paging_init, . - early_paging_init

.section .text.boot, "ax"

.equ UART_DR,    0x00
.equ UART_FR,    0x18
.equ UART_IBRD,  0x24
.equ UART_FBRD,  0x28
.equ UART_LCRH,  0x2C
.equ UART_CR,    0x30
.equ UART_ICR,   0x44

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
    .asciz "\r\nPANIC: r2 is not a DTB (missing -dtb?)\r\n"
.balign 4

.section .text.boot, "ax"
.type setup_mode_stacks, %function
setup_mode_stacks:
    SET_MODE_STACK_PHYS MODE_IRQ, __irq_stack_top__
    SET_MODE_STACK_PHYS MODE_ABT, __abt_stack_top__
    SET_MODE_STACK_PHYS MODE_UND, __und_stack_top__
    SET_MODE_STACK_PHYS MODE_SVC, __svc_stack_top__

    bx      lr
.size setup_mode_stacks, . - setup_mode_stacks

.section .text, "ax"
.type setup_mode_stacks_higher_half, %function
setup_mode_stacks_higher_half:
    ldr     r1, =BOARD_KERNEL_VA_OFFSET

    SET_MODE_STACK_VIRT MODE_IRQ, __irq_stack_top__
    SET_MODE_STACK_VIRT MODE_ABT, __abt_stack_top__
    SET_MODE_STACK_VIRT MODE_UND, __und_stack_top__
    SET_MODE_STACK_VIRT MODE_SVC, __svc_stack_top__

    bx      lr
.size setup_mode_stacks_higher_half, . - setup_mode_stacks_higher_half

.section .text, "ax"
.global higher_half_entry
.type higher_half_entry, %function

higher_half_entry:
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    bge     2f
    str     r2, [r0], #4
    b       1b
2:

    ldr     r0, =vector_table
    mcr     p15, 0, r0, c12, c0, 0
    isb

    bl      setup_mode_stacks_higher_half

    ldr     r3, =early_uart_putc
    mov     r0, #'z'
    blx     r3
    mov     r0, #'u'
    blx     r3
    mov     r0, #'\n'
    blx     r3

    mov     r0, r4
    bl      early
3:
    wfi
    b       3b

.size higher_half_entry, . - higher_half_entry
