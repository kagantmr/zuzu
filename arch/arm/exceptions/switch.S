/**
 * @file
 * @brief Kernel context switch between two tasks' kernel stacks.
 */

#include <generated/asm-offsets.h>

.fpu vfpv4

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

    clrex
    cmp r0, #0
    beq .load_next

    push {r4-r11, lr}

    str sp, [r0, #TASK_KERNEL_SP]

.load_next:
    ldr sp, [r1, #TASK_KERNEL_SP]

    pop {r4-r11, lr}

    bx lr
