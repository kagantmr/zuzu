.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    /* Physical setup */

    /* Disable interrupts */
    cpsid   if

    /* Set up stacks for all modes (at physical addresses) */
    bl      setup_mode_stacks

    /* Save DTB physical address (r4 is callee-saved) */
    ldr     r4, =__dtb_addr__

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
    orr     r0, r5, #0x6B           @ IRGN=0b11, S=1, RGN=0b01, NOS=1
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
    cps     #0x12
    ldr     sp, =__irq_stack_top__

    cps     #0x17
    ldr     sp, =__abt_stack_top__

    cps     #0x1b
    ldr     sp, =__und_stack_top__

    cps     #0x13
    ldr     sp, =__svc_stack_top__

    bx      lr
.size setup_mode_stacks, . - setup_mode_stacks


/* virtual SP setup for all modes */
.section .text, "ax"
.type setup_mode_stacks_higher_half, %function
setup_mode_stacks_higher_half:
    /* VA = PA + (KERNEL_VA_BASE - KERNEL_PA_BASE) = PA + 0x40000000 */
    ldr     r1, =0x40000000

    cps     #0x12
    ldr     r0, =__irq_stack_top__
    add     sp, r0, r1

    cps     #0x17
    ldr     r0, =__abt_stack_top__
    add     sp, r0, r1

    cps     #0x1b
    ldr     r0, =__und_stack_top__
    add     sp, r0, r1

    cps     #0x13
    ldr     r0, =__svc_stack_top__
    add     sp, r0, r1

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

    /* early call */
    ldr     r0, =__dtb_addr__       @ DTB physical address
    bl      early                   @ Jump to C code
3:
    wfi
    b       3b

.size higher_half_entry, . - higher_half_entry
