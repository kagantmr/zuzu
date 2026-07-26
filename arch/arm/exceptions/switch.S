// switch.S - context switch implementation for ARMv7 with VFP support
// context_switch saves/restores CPU state for context switching.

.fpu vfpv4

.global context_switch
// void context_switch(thread_t *prev, thread_t *next)
//    1. Save prev's registers onto prev's kernel stack
//    2. Store stack pointer into prev->kernel_sp
//    3. Load stack pointer from next->kernel_sp
//    4. Restore next's registers from next's kernel stack
//    5. "Return" (which jumps into next)
context_switch:
    clrex
    cmp r0, #0
    beq .load_next

    push {r4-r11, lr}

    str sp, [r0, #12]

.load_next:
    ldr sp, [r1, #12]

    pop {r4-r11, lr}

    bx lr
