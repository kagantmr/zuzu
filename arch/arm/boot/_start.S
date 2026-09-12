@ Shared ARMv7-A boot path. Board differences are six constants in
@ <board>/boot.h plus two optional hooks in <board>/boot_hooks.S.
@
@ _start must be the very first thing in .text.boot: raw-image loaders (QEMU
@ -kernel, the Pi firmware) jump to the first byte of the image, not to the
@ ELF entry point. The linker script keeps it there.

.section .text.boot, "ax"

.equ MODE_IRQ,        0x12
.equ MODE_ABT,        0x17
.equ MODE_UND,        0x1b
.equ MODE_SVC,        0x13
.equ MODE_HYP,        0x1a
#include <arch_impl/armv7_mmu.h>
#include BOARD_BOOT_H
.equ TTBR0_BOOT_ATTRS, TTBR_WALK_ATTRS

@ Derived from the board's two base addresses -- a board that got these
@ inconsistent with each other would fault in ways that are painful to debug.
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
    /* Disable interrupts */
    cpsid   if

    /* Park any core that is not core 0. */
    mrc     p15, 0, r0, c0, c0, 5   @ MPIDR
    ands    r0, r0, #0xFF           @ Aff0 = core number
    beq     1f
0:
    wfe
    b       0b
1:
    /* Leave hyp */
    mrs     r0, cpsr
    and     r1, r0, #0x1F
    cmp     r1, #MODE_HYP
    bne     svc_entry

    mrc     p15, 4, r1, c14, c1, 0  @ CNTHCTL
    orr     r1, r1, #0x3            @ PL1PCTEN | PL1PCEN: PL1 may use timers
    mcr     p15, 4, r1, c14, c1, 0
    mov     r1, #0
    mcrr    p15, 4, r1, r1, c14     @ CNTVOFF = 0

    /* HYP trap controls reset to UNKNOWN. Nothing must trap to HYP after we
     * leave it: a stray HCPTR.TCP10/11 would kill the kernel at its first VFP
     * access, HDCR.TPM at the first PMU access, and so on. */
    mcr     p15, 4, r1, c1, c1, 0   @ HCR   = 0 (no hypercalls/traps)
    mcr     p15, 4, r1, c1, c1, 1   @ HDCR  = 0 (no debug/PMU traps)
    mcr     p15, 4, r1, c1, c1, 2   @ HCPTR = 0 (no coprocessor/VFP traps)
    mcr     p15, 4, r1, c1, c1, 3   @ HSTR  = 0 (no CP15 traps)

    /* Board hook, still in HYP: last chance to touch HYP-only state.
     * Must not touch the stack -- SP_hyp is whatever the firmware left, and a
     * fault here happens before any console or HYP vectors exist, so it hangs
     * silently. Contract: clobbers r1 only, preserves r0 (the CPSR we are
     * about to edit), returns via bx lr. */
    bl      board_hyp_setup

    /* ERET into SVC with IRQ/FIQ masked */
    bic     r0, r0, #0x1F
    orr     r0, r0, #(MODE_SVC | 0xC0)
    msr     spsr_cxsf, r0           @ SPSR_hyp
    ldr     r1, =svc_entry
    msr     elr_hyp, r1
    eret

svc_entry:
    /* Stash the loader's DTB pointer (ARM Linux boot protocol: r2 = DTB
     * physical address) before r0-r3 get clobbered. r4 is callee-saved
     * through everything below. */
    mov     r4, r2

    /* Stacks before the console: early_uart_init and the hooks below use bl. */
    bl      setup_mode_stacks

    /* Console up first so a bad DTB pointer can say so instead of silently
     * mapping garbage. */
    bl      early_uart_init

    /* "zu" now, "zu\n" once the MMU is on -- the only boot feedback on
     * hardware with no debugger attached. */
    mov     r0, #'z'
    bl      early_uart_putc
    mov     r0, #'u'
    bl      early_uart_putc

    /* Sanity-check r4 before trusting it as a DTB pointer: without a DTB,
     * QEMU synthesizes an ATAG list instead, and walking that as an FDT gives
     * a garbage memory map and a crash three functions later in early.c. */
    ldr     r0, [r4]
    ldr     r1, =0xedfe0dd0         @ FDT magic 0xd00dfeed, byte-swapped
                                     @ for a little-endian word load
    cmp     r0, r1
    beq     1f
    bl      dtb_panic
1:

    /* Page table init (r0 = DTB PA so its megabyte gets mapped) */
    mov     r0, r4
    bl      early_paging_init       @ Returns L1 table physical address in r0
    mov     r5, r0                  @ Save TTBR0 base in r5

    /* Set TTBCR = 0 (use TTBR0 for all addresses, N=0) */
    mov     r0, #0
    mcr     p15, 0, r0, c2, c0, 2   @ TTBCR = 0
    isb

    /* TTBR0[31:14] = table base, bits[13:0] = attributes.
     * Inner/outer write-back, write-allocate cacheable. */
    orr     r0, r5, #TTBR0_BOOT_ATTRS @ IRGN=0b11, S=1, RGN=0b01, NOS=1
    mcr     p15, 0, r0, c2, c0, 0   @ TTBR0 = r0
    isb

    /* DACR: Domain 0 = Client (0b01), check permissions */
    mov     r0, #0x1
    mcr     p15, 0, r0, c3, c0, 0   @ DACR = 0x1
    isb

    /* Invalidate unified TLB */
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0   @ TLBIALL
    dsb
    isb

    /* Invalidate instruction cache */
    mcr     p15, 0, r0, c7, c5, 0   @ ICIALLU
    dsb
    isb

    /* Enable MMU in SCTLR */
    mrc     p15, 0, r0, c1, c0, 0   @ Read SCTLR
    orr     r0, r0, #(1 << 0)       @ M bit: Enable MMU
    orr     r0, r0, #(1 << 2)       @ C bit: Enable data cache
    orr     r0, r0, #(1 << 12)      @ I bit: Enable instruction cache
    bic     r0, r0, #(1 << 28)      @ Clear TRE (TEX remap disable)
    bic     r0, r0, #(1 << 1)       @ Clear A (alignment check disable)
    mcr     p15, 0, r0, c1, c0, 0   @ Write SCTLR
    isb
    dsb

    /* higher half jump */
    ldr     r0, =higher_half_entry
    bx      r0

.size _start, . - _start


/* Default board hooks. A board overrides one by defining the same symbol
 * strongly in its own boot_hooks.S; every object is passed to the link
 * explicitly, so the strong definition wins. */
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


/* Bootstrap L1: identity + higher-half sections for the first
 * BOOTSTRAP_RAM_MB of RAM, the megabyte holding the DTB (a bootloader may
 * place it anywhere in RAM, not just within those 8 MB), and the peripheral
 * block (identity only, for the early console). In: r0 = DTB physical
 * address. Out: r0 = L1 table physical address. r4 is never touched here --
 * the caller relies on it surviving, so r2 is the table-base scratch. */
.global early_paging_init
.type early_paging_init, %function

.equ SECTION_NORMAL_DESC, L1_SECT_BOOT_NORMAL
.equ SECTION_DEVICE_DESC, L1_SECT_BOOT_DEVICE

early_paging_init:
    ldr     r2, =early_l1

    /* Zero the full 16KB bootstrap L1 */
    mov     r5, #0
    mov     r6, #0
1:
    cmp     r5, #4096
    bge     2f
    str     r6, [r2, r5, lsl #2]
    add     r5, r5, #1
    b       1b
2:
    /* Map enough RAM to enter C; early.c expands this from the DTB. */
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
    /* Map two sections at the DTB (it may straddle a 1MB boundary), identity
     * and higher-half, so early() can parse it before the full RAM map. */
    lsr     r11, r0, #20            @ DTB megabyte index (physical)
    mov     r5, #0
5:
    cmp     r5, #2
    bge     6f
    add     r12, r11, r5            @ L1 index = PA >> 20
    lsl     r6, r12, #20
    orr     r6, r6, r8              @ normal memory descriptor
    str     r6, [r2, r12, lsl #2]
    add     r12, r12, #VA_OFFSET_IDX
    bic     r12, r12, #0x1000       @ wrap L1 index to 4096 entries
    str     r6, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       5b
6:
    /* Identity-map the peripheral block (device memory) so a C-level
     * arch_early_putc can poke the UART at its physical address after the MMU
     * is on -- this entry survives the RAM identity unmap (which only clears
     * RAM sections) and is carried into the kernel L1 by vmm_bootstrap. */
    ldr     r7, =BOARD_PERIPH_PA_BASE
    ldr     r8, =SECTION_DEVICE_DESC
    mov     r5, #0
    ldr     r10, =BOARD_PERIPH_MB
7:
    cmp     r5, r10
    bge     8f
    add     r11, r7, r5, lsl #20
    orr     r11, r11, r8
    lsr     r12, r11, #20           @ identity: index = PA >> 20
    str     r11, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       7b
8:
    mov     r0, r2
    bx      lr
.size early_paging_init, . - early_paging_init


/* Minimal early PL011 console, physical/identity addressing only (the MMU is
 * off here). Exists to report a bad DTB pointer before anything else can run;
 * drivers/uart/pl011.c takes over post-MMU. */
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
    bl      board_console_pinmux    @ route pins to the UART, if the board must
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
    mov     r0, #0x70               @ LCRH: FEN | WLEN 8 (latches divisors)
    str     r0, [r1, #UART_LCRH]
    ldr     r0, =0x301              @ CR: UARTEN | TXE | RXE
    str     r0, [r1, #UART_CR]
    bx      lr
.size early_uart_init, . - early_uart_init

/* r0 = character. Clobbers r1, r2. Physical/identity addressing only. */
.global early_uart_putc
.type early_uart_putc, %function
early_uart_putc:
    ldr     r1, =BOARD_UART0_BASE
1:
    ldr     r2, [r1, #UART_FR]
    tst     r2, #(1 << 5)           @ TXFF
    bne     1b
    str     r0, [r1, #UART_DR]
    bx      lr
.size early_uart_putc, . - early_uart_putc

/* Prints dtb_panic_msg and hangs. Never returns. Uses r5/r6 (not r0-r4:
 * r4 holds the DTB pointer and early_uart_putc's argument/clobbers use
 * r0-r2) so it survives being called mid-boot without extra save/restore. */
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


/* physical SP setup */
.section .text.boot, "ax"
.type setup_mode_stacks, %function
setup_mode_stacks:
    SET_MODE_STACK_PHYS MODE_IRQ, __irq_stack_top__
    SET_MODE_STACK_PHYS MODE_ABT, __abt_stack_top__
    SET_MODE_STACK_PHYS MODE_UND, __und_stack_top__
    SET_MODE_STACK_PHYS MODE_SVC, __svc_stack_top__

    bx      lr
.size setup_mode_stacks, . - setup_mode_stacks


/* virtual SP setup for all modes */
.section .text, "ax"
.type setup_mode_stacks_higher_half, %function
setup_mode_stacks_higher_half:
    /* VA = PA + KERNEL_VA_OFFSET */
    ldr     r1, =BOARD_KERNEL_VA_OFFSET

    SET_MODE_STACK_VIRT MODE_IRQ, __irq_stack_top__
    SET_MODE_STACK_VIRT MODE_ABT, __abt_stack_top__
    SET_MODE_STACK_VIRT MODE_UND, __und_stack_top__
    SET_MODE_STACK_VIRT MODE_SVC, __svc_stack_top__

    bx      lr
.size setup_mode_stacks_higher_half, . - setup_mode_stacks_higher_half


/* code linked at higher half */
.section .text, "ax"
.global higher_half_entry
.type higher_half_entry, %function

higher_half_entry:
    /* Clear BSS */
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    bge     2f
    str     r2, [r0], #4
    b       1b
2:

    /* Set VBAR to vector_table */
    ldr     r0, =vector_table
    mcr     p15, 0, r0, c12, c0, 0  @ VBAR = vector_table
    isb

    /* Relocate all mode stacks to higher-half VAs before entering C.
     * Pre-MMU stacks were physical/identity addresses, which stop being
     * valid once the identity map is removed. */
    bl      setup_mode_stacks_higher_half

    /* Finish the "zuzu" progress word, then go quiet until the kernel console
     * takes over. early_uart_putc lives in .text.boot at its physical address,
     * out of BL range from the higher half, so call it absolutely through the
     * still-live identity mapping. */
    ldr     r3, =early_uart_putc
    mov     r0, #'z'
    blx     r3
    mov     r0, #'u'
    blx     r3
    mov     r0, #'\n'
    blx     r3

    /* Interrupts stay disabled until kernel IRQ/GIC init is complete. */

    /* early call: r4 has held the DTB physical address since svc_entry */
    mov     r0, r4
    bl      early                   @ Jump to C code
3:
    wfi
    b       3b

.size higher_half_entry, . - higher_half_entry
