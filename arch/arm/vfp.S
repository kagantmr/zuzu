/**
 * @file
 * @brief Saving and loading the VFP registers, in the FpuState layout: d0-d31, then FPSCR.
 */

.fpu vfpv4
.text

/**
 * ArchFpuSaveState(FpuState *state)
 * In:       r0 = state.
 * Clobbers: r0, r1.
 */
.global ArchFpuSaveState
ArchFpuSaveState:
    vstmia r0!, {d0-d15}
    vstmia r0!, {d16-d31}
    vmrs r1, fpscr
    str r1, [r0]
    bx lr

/**
 * ArchFpuRestoreState(const FpuState *state)
 * In:       r0 = state.
 * Clobbers: r0, r1.
 */
.global ArchFpuRestoreState
ArchFpuRestoreState:
    vldmia r0!, {d0-d15}
    vldmia r0!, {d16-d31}
    ldr r1, [r0]
    vmsr fpscr, r1
    bx lr
