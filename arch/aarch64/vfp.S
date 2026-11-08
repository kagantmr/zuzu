/**
 * @file
 * @brief Saving and loading the NEON/FPU registers, in the FpuState layout: q0-q31 then FPCR and FPSR.
 */

.arch_extension fp
.arch_extension simd
.text

/**
 * FpuSaveState(FpuState *state)
 * In:       x0 = state.
 * Clobbers: x0, x1, x2.
 */
.global FpuSaveState
FpuSaveState:
    stp q0, q1, [x0], #32
    stp q2, q3, [x0], #32
    stp q4, q5, [x0], #32
    stp q6, q7, [x0], #32
    stp q8, q9, [x0], #32
    stp q10, q11, [x0], #32
    stp q12, q13, [x0], #32
    stp q14, q15, [x0], #32
    stp q16, q17, [x0], #32
    stp q18, q19, [x0], #32
    stp q20, q21, [x0], #32
    stp q22, q23, [x0], #32
    stp q24, q25, [x0], #32
    stp q26, q27, [x0], #32
    stp q28, q29, [x0], #32
    stp q30, q31, [x0], #32
    mrs x1, fpcr
    mrs x2, fpsr
    stp w1, w2, [x0]
    ret

/**
 * FpuRestoreState(const FpuState *state)
 * In:       x0 = state.
 * Clobbers: x0, x1, x2.
 */
.global FpuRestoreState
FpuRestoreState:
    ldp q0, q1, [x0], #32
    ldp q2, q3, [x0], #32
    ldp q4, q5, [x0], #32
    ldp q6, q7, [x0], #32
    ldp q8, q9, [x0], #32
    ldp q10, q11, [x0], #32
    ldp q12, q13, [x0], #32
    ldp q14, q15, [x0], #32
    ldp q16, q17, [x0], #32
    ldp q18, q19, [x0], #32
    ldp q20, q21, [x0], #32
    ldp q22, q23, [x0], #32
    ldp q24, q25, [x0], #32
    ldp q26, q27, [x0], #32
    ldp q28, q29, [x0], #32
    ldp q30, q31, [x0], #32
    ldp w1, w2, [x0]
    msr fpcr, x1
    msr fpsr, x2
    ret
