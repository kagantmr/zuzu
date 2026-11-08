/**
 * @file
 * @brief AArch64 register frames and the user register view.
 */

#ifndef ARCH_AARCH64_IMPL_REGS_H
#define ARCH_AARCH64_IMPL_REGS_H

#include <compiler.h>
#include <snprintf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief One general-purpose register. */
typedef int64_t Register;

/** @brief printf conversion for a Register or an address: 16 upper-case hex digits. Pair with
 * REG_ARG(). */
#define REG_FMT "%016llX"
/** @brief Converts a Register or an address to the argument type REG_FMT expects. */
#define REG_ARG(r) ((unsigned long long)(uint64_t)(r))

/**
 * @brief Registers saved on exception entry: the trap frame.
 *
 * It sits at the top of the task's kernel stack, at kstack_top - sizeof(CpuState). The size stays
 * a multiple of 16 so that sp stays aligned below it.
 */
typedef struct CpuStateStruct {
    Register x[31];         /**< x0-x30; x29 is fp, x30 is lr. */
    Register sp_el0;        /**< User sp. */
    Register return_pc;     /**< ELR_EL1: address execution resumes at. */
    Register return_pstate; /**< SPSR_EL1: PSTATE restored on return. */
} CpuState;

/** @brief Kernel registers kept across a context switch, on the kernel stack. */
typedef struct {
    Register x19, x20, x21, x22, x23, x24, x25, x26, x27, x28; /**< Saved registers per AAPCS64 */
    Register fp; /* x29 */
    Register lr; /* x30: where ContextSwitch "returns" into this task */
} CpuContext;    /* 12 * 8 = 96 bytes, already a multiple of 16 */

/**
 * @brief Points at general-purpose register @p i (x0-x30) of a frame.
 *
 * @param[in] f Frame.
 * @param[in] i Register index.
 * @return Pointer to the saved register.
 */
static __always_inline Register *ArchGetFromFrame(CpuState *f, unsigned i) { return &f->x[i]; }

/**
 * @brief Address execution resumes at of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFramePc(const CpuState *f) { return f->return_pc; }
/**
 * @brief User sp of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameSp(const CpuState *f)
{
    if ((f->return_pstate & 0xF) == 0) /* EL0t: user stack */
        return f->sp_el0;
    return (Register)((uintptr_t)f + sizeof(CpuState)); /* EL1h: kernel stack before entry */
}

/**
 * @brief lr (x30) of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameLr(const CpuState *f) { return f->x[30]; }
/**
 * @brief Saved PSTATE of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameFlags(const CpuState *f) { return f->return_pstate; }
/**
 * @brief Frame pointer (x29) of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameFp(const CpuState *f) { return f->x[29]; }

/**
 * @brief Reads the current frame pointer (x29).
 *
 * @return The current fp.
 */
static inline Register ArchCurrentFramePointer(void)
{
    Register fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    return fp;
}

/**
 * @brief Reads the current PSTATE fields that SPSR_EL1 would hold: NZCV, DAIF and CurrentEL.
 *
 * @return The flags, laid out like SPSR_EL1.
 */
static inline Register ArchCurrentFlags(void)
{
    Register nzcv, daif, el;
    __asm__ volatile("mrs %0, nzcv\n mrs %1, daif\n mrs %2, CurrentEL"
                     : "=r"(nzcv), "=r"(daif), "=r"(el));
    return nzcv | daif | el | 1u;
}

/** @brief Number of general-purpose registers in TaskRegs (x0-x30). */
#define ARCH_NUM_GP_REGS 31

/** @brief A task's user registers, as TaskGetRegs() returns and TaskSetRegs() takes them. */
typedef struct {
    Register gp[ARCH_NUM_GP_REGS]; /**< x0-x30. */
    Register sp;                   /**< User sp. */
    Register lr;                   /**< User lr; the same as gp[30]. */
    Register pc;                   /**< Address the task resumes at. */
    Register flags;                /**< SPSR; only AARCH64_SPSR_USER_BITS can be set. */
} TaskRegs;

/** @brief SPSR mode field M[3:0] for EL0 (EL0t). */
#define AARCH64_SPSR_MODE_EL0T 0x0u
/** @brief SPSR bits user code may set: N Z C V. */
#define AARCH64_SPSR_USER_BITS 0xF0000000u

/**
 * @brief Copies a frame into the user register view.
 *
 * @param[in]  f   Frame.
 * @param[out] out Registers.
 */
static inline void ArchFrameToRegs(const CpuState *f, TaskRegs *out)
{
    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
        out->gp[i] = f->x[i];
    out->sp = f->sp_el0;
    out->lr = f->x[30];
    out->pc = f->return_pc;
    out->flags = f->return_pstate;
}

/**
 * @brief Copies user registers into a frame, keeping the task at EL0.
 *
 * Bits outside AARCH64_SPSR_USER_BITS are dropped, the mode is forced to EL0t and interrupts stay
 * unmasked. gp[30] gives lr; the lr field is ignored.
 *
 * @param[out] f  Frame to write.
 * @param[in]  in Registers.
 * @return false, leaving @p f unchanged, if pc is not 4-byte aligned.
 */
static inline bool ArchRegsToFrame(CpuState *f, const TaskRegs *in)
{
    if (in->pc & 3u)
        return false;

    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
        f->x[i] = in->gp[i];
    f->sp_el0 = in->sp;
    f->return_pc = in->pc;
    f->return_pstate = (in->flags & AARCH64_SPSR_USER_BITS) | AARCH64_SPSR_MODE_EL0T;
    return true;
}

/**
 * @brief Names the exception level and stack pointer in an SPSR.
 *
 * @param[in] spsr SPSR value.
 * @return "EL0t", "EL1t", "EL1h" and so on, or "???".
 */
static inline const char *aarch64_spsr_mode_name(Register spsr)
{
    if (spsr & (1u << 4))
        return "AArch32";
    switch (spsr & 0xFu) {
    case 0x0u:
        return "EL0t";
    case 0x4u:
        return "EL1t";
    case 0x5u:
        return "EL1h";
    case 0x8u:
        return "EL2t";
    case 0x9u:
        return "EL2h";
    default:
        return "???";
    }
}

/**
 * @brief Formats an SPSR for logs, e.g. "[EL1h D A I F nZCv]", with masked exceptions in capitals.
 *
 * @param[out] buf   Output buffer.
 * @param[in]  bufsz Size of @p buf.
 * @param[in]  flags SPSR value.
 */
static inline void ArchFlagsDecode(char *buf, size_t bufsz, Register flags)
{
    (void)snprintf(buf, bufsz, "[%s %c%c%c%c %c%c%c%c]", aarch64_spsr_mode_name(flags),
                   (flags >> 9) & 1u ? 'D' : 'd', (flags >> 8) & 1u ? 'A' : 'a',
                   (flags >> 7) & 1u ? 'I' : 'i', (flags >> 6) & 1u ? 'F' : 'f',
                   (flags >> 31) & 1u ? 'N' : 'n', (flags >> 30) & 1u ? 'Z' : 'z',
                   (flags >> 29) & 1u ? 'C' : 'c', (flags >> 28) & 1u ? 'V' : 'v');
}

/**
 * @brief Tells whether flags belong to an interrupt handler.
 *
 * AArch64 has no IRQ mode, so PSTATE cannot tell.
 *
 * @param[in] flags SPSR value.
 * @return false.
 */
static inline bool ArchFlagsInIrqContext(Register flags)
{
    (void)flags;
    return false;
}

#endif // ARCH_AARCH64_IMPL_REGS_H
