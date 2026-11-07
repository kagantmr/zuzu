/**
 * @file
 * @brief Saving and loading the VFP registers, in the FpuState layout: d0-d31, then FPSCR.
 */

.fpu vfpv4
.text

/**
 * FpuSaveState(FpuState *state)
 * In:       r0 = state.
 * Clobbers: r0, r1.
 */
.global FpuSaveState
FpuSaveState:
    vstmia r0!, {d0-d15}
    vstmia r0!, {d16-d31}
    vmrs r1, fpscr
    str r1, [r0]
    bx lr

/**
 * FpuRestoreState(const FpuState *state)
 * In:       r0 = state.
 * Clobbers: r0, r1.
 */
.global FpuRestoreState
FpuRestoreState:
    vldmia r0!, {d0-d15}
    vldmia r0!, {d16-d31}
    ldr r1, [r0]
    vmsr fpscr, r1
    bx lr
