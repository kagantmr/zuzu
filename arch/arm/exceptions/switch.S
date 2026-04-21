@ void context_switch(process_t *prev, process_t *next)
@    1. Save prev's registers onto prev's kernel stack
@    2. Store stack pointer into prev->kernel_sp
@    3. Load stack pointer from next->kernel_sp
@    4. Restore next's registers from next's kernel stack
@    5. "Return" (which jumps into next)
.fpu vfpv4

.global context_switch
context_switch:
    cmp r0, #0
    beq .load_next

    push {r4-r11, lr}

    @ save VFP: 16 double regs + FPSCR = 132 bytes
    vpush {d0-d15}
    vmrs r2, fpscr
    push {r2}

    str sp, [r0, #12]

.load_next:
    ldr sp, [r1, #12]

    @ restore VFP
    pop {r2}
    vmsr fpscr, r2
    vpop {d0-d15}

    pop {r4-r11, lr}
    bx lr