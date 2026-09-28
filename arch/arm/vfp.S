.fpu vfpv4
.text

// void ArchFpuSaveState(fpu_state_t *state)
.global ArchFpuSaveState
ArchFpuSaveState:
    vstmia r0!, {d0-d15}
    vstmia r0!, {d16-d31}
    vmrs r1, fpscr
    str r1, [r0]
    bx lr

// void ArchFpuRestoreState(const fpu_state_t *state)
.global ArchFpuRestoreState
ArchFpuRestoreState:
    vldmia r0!, {d0-d15}
    vldmia r0!, {d16-d31}
    ldr r1, [r0]
    vmsr fpscr, r1
    bx lr
