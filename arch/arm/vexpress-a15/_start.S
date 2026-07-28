@ _start.s - ARMv7-A boot code for Versatile Express A15

.section .text.boot, "ax"
.global early_paging_init
.type early_paging_init, %function

.equ RAM_PA_BASE,        0x80000000
.equ BOOTSTRAP_RAM_MB,   8
.equ RAM_LOW_IDX_BASE,   0x800
.equ RAM_HIGH_IDX_BASE,  0xC00
.equ SECTION_NORMAL_DESC,0x11C0E
.equ VA_OFFSET_IDX,      0x400     @ KERNEL_VA_OFFSET (0x40000000) >> 20

/* In: r0 = DTB physical address. Out: r0 = L1 table physical address.
 * r4 is never touched here (the caller relies on it surviving this call
 * unmodified — see svc_entry) — r2 is this function's own table-base
 * scratch register instead. */
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
    str     r11, [r2, r12, lsl #2]
    add     r12, r5, #RAM_HIGH_IDX_BASE
    str     r11, [r2, r12, lsl #2]
    add     r5, r5, #1
    b       3b
4:
    /* Map two sections at the DTB (may straddle a 1MB boundary), identity
     * and higher-half, so early() can parse it before the full RAM map.
     * A real bootloader can place (or, as u-boot's bootm does, relocate)
     * the DTB anywhere in RAM, not just within the first BOOTSTRAP_RAM_MB,
     * e.g. near the top of RAM. */
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
    mov     r0, r2
    bx      lr
.size early_paging_init, . - early_paging_init

.global _start
.type _start, %function

.equ MODE_IRQ,        0x12
.equ MODE_ABT,        0x17
.equ MODE_UND,        0x1b
.equ MODE_SVC,        0x13
.equ MODE_HYP,        0x1a
.equ TTBR0_BOOT_ATTRS, 0x6B
.equ KERNEL_VA_OFFSET, 0x40000000
.equ FDT_MAGIC_LE,    0xEDFE0DD0     @ 0xD00DFEED read little-endian

.macro SET_MODE_STACK_PHYS mode, top_sym
    cps     #\mode
    ldr     sp, =\top_sym
.endm

.macro SET_MODE_STACK_VIRT mode, top_sym
    cps     #\mode
    ldr     r0, =\top_sym
    add     sp, r0, r1
.endm

_start:
    /* Physical setup */

    /* Disable interrupts */
    cpsid   if

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

    mcr     p15, 4, r1, c1, c1, 0   @ no hypercalls/traps
    mcr     p15, 4, r1, c1, c1, 1   @ no debug/PMU traps
    mcr     p15, 4, r1, c1, c1, 2   @ no coprocessor/VFP traps
    mcr     p15, 4, r1, c1, c1, 3   @ no CP15 traps

    bic     r0, r0, #0x1F
    orr     r0, r0, #(MODE_SVC | 0xC0)
    msr     spsr_cxsf, r0           @ SPSR_hyp
    ldr     r1, =svc_entry
    msr     elr_hyp, r1
    eret

svc_entry:
    /* Stash the bootloader's DTB pointer (Linux/u-boot ARM boot protocol:
     * r2 = DTB physical address) before r0-r3 get clobbered. r4 is
     * callee-saved through everything below. */
    mov     r4, r2

    /* Set up stacks for all modes (at physical addresses) */
    bl      setup_mode_stacks

    /* Validate it: a real bootloader sets r2, but QEMU's direct -kernel/
     * -dtb ELF boot (no bootloader, `make run`) never populates the Linux
     * boot registers for an ELF load, leaving r2 == 0 — guard against that
     * before dereferencing it (no exception vectors exist yet to catch a
     * fault on address 0), then fall back to the link-time address that
     * path is known to drop the DTB at instead. */
    cmp     r4, #0
    beq     2f
    ldr     r1, =FDT_MAGIC_LE
    ldr     r0, [r4]
    cmp     r0, r1
    beq     1f
2:
    ldr     r4, =__dtb_fallback__
1:
    /* Do page table init in C code */
    mov     r0, r4                  @ Pass DTB physical address
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
    /* post-MMU code*/

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
     * Pre-MMU stacks were set to physical/identity addresses (0x80...).
     * After identity removal, those become invalid.
     */
    bl      setup_mode_stacks_higher_half

    /* Keep interrupts disabled until kernel IRQ/GIC init is complete */

    /* early call: r4 has held the DTB physical address since _start */
    mov     r0, r4
    bl      early                   @ Jump to C code
3:
    wfi
    b    3b

.size higher_half_entry, . - higher_half_entry
