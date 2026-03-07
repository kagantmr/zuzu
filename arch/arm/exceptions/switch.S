@ void context_switch(process_t *prev, process_t *next)
@    1. Save prev's registers onto prev's kernel stack
@    2. Store stack pointer into prev->kernel_sp
@    3. Load stack pointer from next->kernel_sp
@    4. Restore next's registers from next's kernel stack
@    5. "Return" (which jumps into next)

.global context_switch
context_switch:
    cmp r0, #0              @ is prev NULL?
    beq .load_next          @ skip save if so
    push {r4-r11, lr}
    str sp, [r0, #12]
.load_next:
    ldr sp, [r1, #12]
    pop {r4-r11, lr}
    bx lr