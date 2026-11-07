/**
 * @file
 * @brief AArch64 exception vector table; VBAR points here once the kernel runs in the higher half.
 */

#include <generated/asm-offsets.h>

/** 2KB aligned, as VBAR requires. */
.section .vectors, "ax"
.align 11
.global vector_table

.macro ventry label
    .balign 0x80
    b \label
.endm

/**
 * SAVE_STATE
 *
 * Pushes a CpuState onto the current stack: x0-x30, then SP_EL0, ELR_EL1 and SPSR_EL1.
 * Leaves sp pointing at the frame.
 */
.macro SAVE_STATE
    sub sp, sp, #CPU_STATE_SIZE
    stp x0,  x1,  [sp, #16 * 0]
    stp x2,  x3,  [sp, #16 * 1]
    stp x4,  x5,  [sp, #16 * 2]
    stp x6,  x7,  [sp, #16 * 3]
    stp x8,  x9,  [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x19, [sp, #16 * 9]
    stp x20, x21, [sp, #16 * 10]
    stp x22, x23, [sp, #16 * 11]
    stp x24, x25, [sp, #16 * 12]
    stp x26, x27, [sp, #16 * 13]
    stp x28, x29, [sp, #16 * 14]

    mrs x21, sp_el0
    mrs x22, elr_el1
    mrs x23, spsr_el1
    stp x30, x21, [sp, #CPU_STATE_X30]
    stp x22, x23, [sp, #CPU_STATE_PC]
.endm

/**
 * RESTORE_STATE
 *
 * Pops the CpuState at sp back into the registers, SP_EL0, ELR_EL1 and SPSR_EL1.
 * Follow it with eret.
 */
.macro RESTORE_STATE
    ldp x22, x23, [sp, #CPU_STATE_PC]
    ldp x30, x21, [sp, #CPU_STATE_X30]
    msr sp_el0, x21
    msr elr_el1, x22
    msr spsr_el1, x23

    ldp x0,  x1,  [sp, #16 * 0]
    ldp x2,  x3,  [sp, #16 * 1]
    ldp x4,  x5,  [sp, #16 * 2]
    ldp x6,  x7,  [sp, #16 * 3]
    ldp x8,  x9,  [sp, #16 * 4]
    ldp x10, x11, [sp, #16 * 5]
    ldp x12, x13, [sp, #16 * 6]
    ldp x14, x15, [sp, #16 * 7]
    ldp x16, x17, [sp, #16 * 8]
    ldp x18, x19, [sp, #16 * 9]
    ldp x20, x21, [sp, #16 * 10]
    ldp x22, x23, [sp, #16 * 11]
    ldp x24, x25, [sp, #16 * 12]
    ldp x26, x27, [sp, #16 * 13]
    ldp x28, x29, [sp, #16 * 14]
    add sp, sp, #CPU_STATE_SIZE
.endm

vector_table:
    ventry bad_el1t_sync // 0x000  EL1, SP_EL0, sync
    ventry bad_el1t_irq  // 0x080  EL1, SP_EL0, IRQ
    ventry bad_el1t_fiq  // 0x100  EL1, SP_EL0, FIQ
    ventry el1_serror   // 0x180  EL1, SP_EL0, SError

    ventry el1_sync     // 0x200  EL1, SP_EL1, sync

    ventry el1_irq      // 0x280  EL1, SP_EL1, IRQ

    ventry bad_el1h_fiq  // 0x300  EL1, SP_EL1, FIQ
    ventry el1_serror   // 0x380  EL1, SP_EL1, SError

    ventry el0_sync     // 0x400  EL0 AArch64, sync
    ventry el0_irq      // 0x480  EL0 AArch64, IRQ
    ventry bad_el0_fiq   // 0x500  EL0 AArch64, FIQ
    ventry el1_serror   // 0x580  EL0 AArch64, SError

    // zuzu currently does NOT have bwds compat with AArch32. 
    // AArch32 mode isn't a thing
    ventry el0_aarch32  // 0x600  EL0 AArch32, sync
    ventry el0_aarch32  // 0x680  EL0 AArch32, IRQ
    ventry el0_aarch32  // 0x700  EL0 AArch32, FIQ
    ventry el1_serror   // 0x780  EL0 AArch32, SError

el1_sync:
    SAVE_STATE
    mov x0, sp
    bl KernelSyncException
    RESTORE_STATE
    eret

/**
 * BAD_VECTOR label, offset
 *
 * Entry for a vector zuzu never expects: saves the frame and panics, naming the slot by its
 * offset in the table.
 */
.macro BAD_VECTOR label, offset
\label:
    SAVE_STATE
    mov x0, sp
    mov x1, #\offset
    bl BadVector
    b .
.endm

    BAD_VECTOR bad_el1t_sync, 0x000
    BAD_VECTOR bad_el1t_irq,  0x080
    BAD_VECTOR bad_el1t_fiq,  0x100
    BAD_VECTOR bad_el1h_fiq,  0x300
    BAD_VECTOR el0_sync,      0x400
    BAD_VECTOR el0_irq,       0x480
    BAD_VECTOR bad_el0_fiq,   0x500

el0_aarch32:
    SAVE_STATE
    mov x0, sp
    bl AArch32Entry
    b .
  
el1_irq:
    SAVE_STATE
    bl ArchIrqDispatch
    RESTORE_STATE
    eret

el1_serror:                       // used for 0x180, 0x380, 0x580, 0x780
    SAVE_STATE
    mov x0, sp
    bl SErrorException        // never returns
    b .
