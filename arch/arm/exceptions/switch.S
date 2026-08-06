// switch.S - context switch implementation for ARMv7 with VFP support
// context_switch saves/restores CPU state for context switching.

.fpu vfpv4

#ifdef TIME_MEASURE
.global ctx_switch_start
.global ctx_switch_cost
#endif

.global context_switch
// void context_switch(thread_t *prev, thread_t *next)
//    1. Save prev's registers onto prev's kernel stack
//    2. Store stack pointer into prev->kernel_sp
//    3. Load stack pointer from next->kernel_sp
//    4. Restore next's registers from next's kernel stack
//    5. "Return" (which jumps into next)
context_switch:

#ifdef TIME_MEASURE
    isb
    mrc p15, 0, r12, c9, c13, 0   // Read PMCCNTR
    ldr r2, =ctx_switch_start     // Load address of global start variable
    str r12, [r2]                 // Save start time across the thread boundary
    isb
#endif

    clrex
    cmp r0, #0
    beq .load_next

    push {r4-r11, lr}

    str sp, [r0, #12]

.load_next:
    ldr sp, [r1, #12]

    pop {r4-r11, lr}

#ifdef TIME_MEASURE
    isb
    mrc p15, 0, r3, c9, c13, 0    // Read PMCCNTR end time into r3
    ldr r12, =ctx_switch_start
    ldr r12, [r12]                // Retrieve Thread A's start time
    sub r3, r3, r12               // r3 = end - start
    ldr r12, =ctx_switch_cost
    str r3, [r12]                 // Store final cycle count to global cost variable
    isb
#endif

    bx lr
