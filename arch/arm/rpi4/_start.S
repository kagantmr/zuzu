@ _start.S - ARMv7-A (AArch32) boot code for Raspberry Pi 4 (BCM2711)
@
@ Differences from the vexpress port:
@  - The firmware jumps to the first byte of the flat binary, so _start must
@    be at the top of .text.boot (the ELF entry point is not consulted).
@  - Only core 0 is expected here (the firmware stub parks cores 1-3 on the
@    spin table), but a MPIDR guard parks any straggler as insurance.
@  - The firmware enters 32-bit kernels in HYP mode; we do per-core timer
@    setup there (CNTVOFF/CNTHCTL are HYP-only) and drop to SVC.
@  - The DTB address arrives in r2 (validated by FDT magic) instead of being
@    a link-time constant.
@  - The PL011 console is brought up here in software: GPIO14/15 to ALT0,
@    pulls off, mini-UART held off, divisors for 115200 @ 48 MHz UARTCLK.

.section .text.boot, "ax"

.equ MODE_IRQ,        0x12
.equ MODE_ABT,        0x17
.equ MODE_UND,        0x1b
.equ MODE_SVC,        0x13
.equ MODE_HYP,        0x1a
.equ TTBR0_BOOT_ATTRS, 0x6B
.equ KERNEL_VA_OFFSET, 0xC0000000

.equ FDT_MAGIC_LE,    0xEDFE0DD0     @ 0xD00DFEED read little-endian

@ BCM2711 in "low peripheral" mode
.equ GPIO_BASE,       0xFE200000
.equ AUX_ENABLES,     0xFE215004
.equ UART0_BASE,      0xFE201000

.equ GPFSEL1_OFF,     0x04
.equ GPIO_PUP_PDN0_OFF, 0xE4

.equ UART_DR,         0x00
.equ UART_FR,         0x18
.equ UART_IBRD,       0x24
.equ UART_FBRD,       0x28
.equ UART_LCRH,       0x2C
.equ UART_CR,         0x30
.equ UART_ICR,        0x44

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
    /* Mask IRQ/FIQ (valid in HYP and SVC alike) */
    cpsid   if

    /* Park any core that is not core 0. On real hardware the firmware stub
     * parks cores 1-3 before we run; this guard covers loaders that don't. */
    mrc     p15, 0, r0, c0, c0, 5   @ MPIDR
    ands    r0, r0, #0xFF           @ Aff0 = core number
    beq     1f
0:
    wfe
    b       0b
1:
    /* If the firmware entered us in HYP, do the HYP-only setup and drop to
     * SVC. The kernel ticks off the virtual timer (CNTV), whose offset and
     * PL1 accessibility can only be programmed from HYP. */
    mrs     r0, cpsr
    and     r1, r0, #0x1F
    cmp     r1, #MODE_HYP
    bne     svc_entry

    mrc     p15, 4, r1, c14, c1, 0  @ CNTHCTL
    orr     r1, r1, #0x3            @ PL1PCTEN | PL1PCEN: PL1 may use timers
    mcr     p15, 4, r1, c14, c1, 0
    mov     r1, #0
    mcrr    p15, 4, r1, r1, c14     @ CNTVOFF = 0

    /* The HYP trap controls reset to UNKNOWN values. Nothing must trap to
     * HYP after we leave it (no HYP vectors exist): a stray HCPTR.TCP10/11
     * would kill the kernel at its first VFP access (vfp_init), HDCR.TPM at
     * the first PMU access, and so on. */
    mcr     p15, 4, r1, c1, c1, 0   @ HCR   = 0 (no hypercalls/traps)
    mcr     p15, 4, r1, c1, c1, 1   @ HDCR  = 0 (no debug/PMU traps)
    mcr     p15, 4, r1, c1, c1, 2   @ HCPTR = 0 (no coprocessor/VFP traps)
    mcr     p15, 4, r1, c1, c1, 3   @ HSTR  = 0 (no CP15 traps)

    /* Canary vectors: nothing should reach HYP after this point, but if
     * something does, flood the UART with a letter naming the vector
     * instead of hanging silently. HYP's own MMU is off, so the canary
     * runs at physical addresses whatever PL1's page tables look like. */
    ldr     r1, =hyp_vectors
    mcr     p15, 4, r1, c12, c0, 0  @ HVBAR
    isb

    /* ERET into SVC with IRQ/FIQ masked */
    bic     r0, r0, #0x1F
    orr     r0, r0, #(MODE_SVC | 0xC0)
    msr     spsr_cxsf, r0           @ SPSR_hyp
    ldr     r1, =svc_entry
    msr     elr_hyp, r1
    eret

svc_entry:
    /* Stash the firmware's DTB pointer before r0-r3 get clobbered
     * (r4 is callee-saved through every call below). */
    mov     r4, r2

    /* Set up stacks for all modes (at physical addresses) */
    bl      setup_mode_stacks

    /* Console up first: everything after this can report failure. */
    bl      early_uart_init
    mov     r0, #'z'
    bl      early_uart_putc

    /* Validate the DTB pointer: firmware passes it in r2; QEMU's non-Linux
     * ELF boot leaves r2 unset but drops the DTB at __dtb_fallback__. */
    ldr     r1, =FDT_MAGIC_LE
    ldr     r0, [r4]
    cmp     r0, r1
    beq     dtb_ok
    ldr     r4, =__dtb_fallback__
    ldr     r0, [r4]
    cmp     r0, r1
    beq     dtb_ok
    /* No DTB anywhere we know of: say so and park. */
    mov     r0, #'?'
    bl      early_uart_putc
2:
    wfe
    b       2b

dtb_ok:
    mov     r0, #'u'
    bl      early_uart_putc

    /* Do page table init (r0 = DTB PA so its megabyte gets mapped) */
    mov     r0, r4
    bl      early_paging_init       @ Returns L1 table physical address in r0
    mov     r5, r0                  @ Save TTBR0 base in r5

    /* MMU enabling code */

    /* Set TTBCR = 0 (use TTBR0 for all addresses, N=0) */
    mov     r0, #0
    mcr     p15, 0, r0, c2, c0, 2   @ TTBCR = 0
    isb

    /* Set TTBR0 to our page table base */
    /* TTBR0[31:14] = table base, bits[13:0] = attributes */
    /* Use inner/outer write-back, write-allocate cacheable */
    orr     r0, r5, #TTBR0_BOOT_ATTRS @ IRGN=0b11, S=1, RGN=0b01, NOS=1
    mcr     p15, 0, r0, c2, c0, 0   @ TTBR0 = r0
    isb

    /* Set DACR: Domain 0 = Client (0b01), check permissions */
    mov     r0, #0x1                @ Domain 0 = Client access
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


/* Bootstrap L1: identity + higher-half sections for the first
 * BOOTSTRAP_RAM_MB of RAM, the megabyte holding the DTB (which the
 * bootloader may have placed well above 8 MB), and the peripheral block
 * (identity only) so the early UART keeps working until the VMM takes over.
 * In: r0 = DTB physical address. Out: r0 = L1 table physical address.
 * early.c expands the RAM mapping from the DTB afterwards. */
.global early_paging_init
.type early_paging_init, %function

.equ RAM_PA_BASE,        0x00000000
.equ BOOTSTRAP_RAM_MB,   8
.equ RAM_LOW_IDX_BASE,   0x000
.equ RAM_HIGH_IDX_BASE,  0xC00
.equ SECTION_NORMAL_DESC,0x11C0E
.equ SECTION_DEVICE_DESC,0xC16      @ shared device, AP=RW(PL1), XN
.equ PERIPH_PA_BASE,     0xFE000000 @ low-peripheral mode: 0xFE0..0xFFF
.equ PERIPH_MB,          32

early_paging_init:
    ldr     r3, =early_l1

    /* Zero the full 16KB bootstrap L1 */
    mov     r5, #0
    mov     r6, #0
1:
    cmp     r5, #4096
    bge     2f
    str     r6, [r3, r5, lsl #2]
    add     r5, r5, #1
    b       1b
2:
    /* Map enough RAM to enter C; early.c expands this from the DTB. */
    ldr     r7, =RAM_PA_BASE
    ldr     r8, =SECTION_NORMAL_DESC
    mov     r5, #0
    ldr     r10, =BOOTSTRAP_RAM_MB
3:
    cmp     r5, r10
    bge     4f
    add     r11, r7, r5, lsl #20
    orr     r11, r11, r8
    add     r12, r5, #RAM_LOW_IDX_BASE
    str     r11, [r3, r12, lsl #2]
    add     r12, r5, #RAM_HIGH_IDX_BASE
    str     r11, [r3, r12, lsl #2]
    add     r5, r5, #1
    b       3b
4:
    /* Map two sections at the DTB (may straddle a 1MB boundary), identity
     * and higher-half, so early() can parse it before the full RAM map. */
    lsr     r11, r0, #20            @ DTB megabyte index
    mov     r5, #0
5:
    cmp     r5, #2
    bge     6f
    add     r12, r11, r5            @ L1 index = PA >> 20
    lsl     r6, r12, #20
    orr     r6, r6, r8              @ normal memory descriptor
    str     r6, [r3, r12, lsl #2]
    add     r12, r12, #RAM_HIGH_IDX_BASE  @ + (KERNEL_VA_OFFSET >> 20)
    bic     r12, r12, #0x1000       @ wrap L1 index to 4096 entries
    str     r6, [r3, r12, lsl #2]
    add     r5, r5, #1
    b       5b
6:
    /* Identity-map the peripheral block (device memory) for the early UART.
     * These entries sit in the ioremap VA range of this bootstrap table
     * only; the VMM builds its own L1 before ioremap hands out VAs there. */
    ldr     r7, =PERIPH_PA_BASE
    ldr     r8, =SECTION_DEVICE_DESC
    mov     r5, #0
    ldr     r10, =PERIPH_MB
7:
    cmp     r5, r10
    bge     8f
    add     r11, r7, r5, lsl #20
    orr     r11, r11, r8
    lsr     r12, r11, #20           @ identity: index = PA >> 20
    str     r11, [r3, r12, lsl #2]
    add     r5, r5, #1
    b       7b
8:
    mov     r0, r3
    bx      lr
.size early_paging_init, . - early_paging_init


/* Early PL011 console bring-up, all in software (no config.txt overlays):
 * route GPIO14/15 to ALT0 (TXD0/RXD0), drop their pulls, keep the mini-UART
 * disabled, then program the PL011 for 115200 8N1 assuming the firmware's
 * default 48 MHz UARTCLK (divisor 26 + 3/64). Clobbers r0-r3. */
.type early_uart_init, %function
early_uart_init:
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

    /* PL011: disable, clear interrupts, 115200 @ 48 MHz, 8N1 + FIFO, enable */
    ldr     r1, =UART0_BASE
    mov     r0, #0
    str     r0, [r1, #UART_CR]
    ldr     r0, =0x7FF
    str     r0, [r1, #UART_ICR]
    mov     r0, #26                 @ IBRD: 48e6 / (16 * 115200) = 26.042
    str     r0, [r1, #UART_IBRD]
    mov     r0, #3                  @ FBRD: round(0.042 * 64)
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
    ldr     r1, =UART0_BASE
1:
    ldr     r2, [r1, #UART_FR]
    tst     r2, #(1 << 5)           @ TXFF
    bne     1b
    str     r0, [r1, #UART_DR]
    bx      lr
.size early_uart_putc, . - early_uart_putc


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
    ldr     r1, =UART0_BASE
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


/* physical SP setup */
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
    ldr     r1, =KERNEL_VA_OFFSET

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
    /* post-MMU code (identity + peripheral maps still live in early_l1) */

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
     * Pre-MMU stacks were set to physical/identity addresses.
     * After identity removal, those become invalid.
     */
    bl      setup_mode_stacks_higher_half

    /* Keep interrupts disabled until kernel IRQ/GIC init is complete */

    /* 'zu' printed pre-MMU; complete the word before going quiet until the
     * kernel console takes over. early_uart_putc lives in .text.boot at its
     * physical address (out of BL range from the higher half), so call it
     * absolutely through the still-live identity mapping. */
    ldr     r3, =early_uart_putc
    mov     r0, #'z'
    blx     r3
    mov     r0, #'u'
    blx     r3
    mov     r0, #'\n'
    blx     r3

    /* early call: r4 has held the DTB physical address since svc_entry */
    mov     r0, r4
    bl      early                   @ Jump to C code
3:
    wfi
    b       3b

.size higher_half_entry, . - higher_half_entry
