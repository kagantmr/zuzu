#include <generated/asm-offsets.h>

.fpu vfpv4

#ifdef CONFIG_TIME_MEASURE
.global ctx_switch_start
.global ctx_switch_cost
#endif

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
