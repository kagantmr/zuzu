.fpu vfpv4
.text

// void arch_fpu_save(fpu_state_t *state)
.global arch_fpu_save
arch_fpu_save:
    vstmia r0!, {d0-d15}
    vstmia r0!, {d16-d31}
    vmrs r1, fpscr
    str r1, [r0]
    bx lr

// void arch_fpu_restore(const fpu_state_t *state)
.global arch_fpu_restore
arch_fpu_restore:
    vldmia r0!, {d0-d15}
    vldmia r0!, {d16-d31}
    ldr r1, [r0]
    vmsr fpscr, r1
    bx lr
