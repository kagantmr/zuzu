/**
 * @file
 * @brief Kernel context switch between two tasks' kernel stacks.
 */

#include <generated/asm-offsets.h>

.fpu vfpv4

/**
 * With CONFIG_TIME_MEASURE, ctx_switch_cost receives the cycle count of the last
 * switch and ctx_switch_start is scratch for it.
 */
#ifdef CONFIG_TIME_MEASURE
.global ctx_switch_start
.global ctx_switch_cost
#endif

/**
 * ContextSwitch(TaskObject *prev, TaskObject *next)
 *
 * Pushes r4-r11 and lr (a CpuContext) on prev's kernel stack and stores sp in
 * prev->kernel_sp, then loads next->kernel_sp, pops next's CpuContext and returns
 * into next. Clears the exclusive monitor so a half-done ldrex/strex in prev
 * cannot succeed in next.
 *
 * In:   r0 = prev, or NULL to switch without saving (first switch, dead task).
 *       r1 = next.
 * Out:  Returns in next's context, to the lr saved in its CpuContext.
 */
.global ContextSwitch
ContextSwitch:

#ifdef CONFIG_TIME_MEASURE
    isb
    mrc p15, 0, r12, c9, c13, 0
    ldr r2, =ctx_switch_start
    str r12, [r2]
    isb
#endif

    clrex
    cmp r0, #0
    beq .load_next

    push {r4-r11, lr}

    str sp, [r0, #TASK_KERNEL_SP]

.load_next:
    ldr sp, [r1, #TASK_KERNEL_SP]

    pop {r4-r11, lr}

#ifdef CONFIG_TIME_MEASURE
    isb
    mrc p15, 0, r3, c9, c13, 0
    ldr r12, =ctx_switch_start
    ldr r12, [r12]
    sub r3, r3, r12
    ldr r12, =ctx_switch_cost
    str r3, [r12]
    isb
#endif

    bx lr
