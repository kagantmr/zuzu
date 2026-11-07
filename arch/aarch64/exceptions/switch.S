/**
 * @file
 * @brief Kernel context switch between two tasks' kernel stacks.
 */

#include <generated/asm-offsets.h>

/**
 * With CONFIG_TIME_MEASURE, ctx_switch_cost receives the cycle count of the last switch.
 */
#ifdef CONFIG_TIME_MEASURE
.bss
.balign 8
.global ctx_switch_cost
ctx_switch_cost:
    .quad 0
#endif

.text

/**
 * ContextSwitch(TaskObject *prev, TaskObject *next)
 *
 * Pushes x19-x30 (a CpuContext) on prev's kernel stack and stores sp in prev->kernel_sp,
 * then loads next->kernel_sp, pops next's CpuContext and returns into next. Clears the
 * exclusive monitor so a half-done ldxr/stxr in prev cannot succeed in next.
 *
 * In:   x0 = prev, or NULL to switch without saving (first switch, dead task).
 *       x1 = next.
 * Out:  Returns in next's context, to the x30 saved in its CpuContext.
 */
.global ContextSwitch
ContextSwitch:

#ifdef CONFIG_TIME_MEASURE
    isb
    mrs x3, pmccntr_el0
    isb
#endif

    clrex
    cbz x0, 1f

    sub sp, sp, #96
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]

    mov x2, sp
    str x2, [x0, #TASK_KERNEL_SP]

1:
    ldr x2, [x1, #TASK_KERNEL_SP]
    mov sp, x2

    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #96

#ifdef CONFIG_TIME_MEASURE
    isb
    mrs x4, pmccntr_el0
    sub x4, x4, x3
    adrp x5, ctx_switch_cost
    str x4, [x5, :lo12:ctx_switch_cost]
    isb
#endif

    ret
