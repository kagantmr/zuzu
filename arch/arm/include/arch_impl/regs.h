/**
 * @file
 * @brief ARMv7-A register frames and the user register view.
 */

#ifndef ARCH_ARM_IMPL_REGS_H
#define ARCH_ARM_IMPL_REGS_H

#include <compiler.h>
#include <snprintf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief One general-purpose register. */
typedef int32_t Register;

/**
 * @brief User registers saved on exception entry: the trap frame.
 *
 * It sits at the top of the task's kernel stack, at kstack_top - sizeof(CpuState).
 */
typedef struct CpuStateStruct {
    Register r[13];      /**< r0-r12. */
    Register sp_usr;     /**< User-mode sp (r13_usr). */
    Register lr_usr;     /**< User-mode lr (r14_usr). */
    Register return_pc;  /**< Address execution resumes at. */
    Register return_cpsr; /**< CPSR restored on return (the saved SPSR). */
} CpuState;

/** @brief Kernel registers kept across a context switch, on the kernel stack. */
typedef struct {
    Register r4;  /**< Callee-saved r4. */
    Register r5;  /**< Callee-saved r5. */
    Register r6;  /**< Callee-saved r6. */
    Register r7;  /**< Callee-saved r7. */
    Register r8;  /**< Callee-saved r8. */
    Register r9;  /**< Callee-saved r9. */
    Register r10; /**< Callee-saved r10. */
    Register r11; /**< Callee-saved r11 (fp). */
    Register lr;  /**< Where the switch returns to. */
} CpuContext;

/**
 * @brief Points at argument/result register @p i (r0-r12) of a frame.
 *
 * @param[in] f Frame.
 * @param[in] i Register index.
 * @return Pointer to the saved register.
 */
static __always_inline Register *ArchGetFromFrame(CpuState *f, unsigned i) { return &f->r[i]; }

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
static __always_inline Register ArchFrameSp(const CpuState *f) { return f->sp_usr; }
/**
 * @brief User lr of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameLr(const CpuState *f) { return f->lr_usr; }
/**
 * @brief Saved CPSR of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameFlags(const CpuState *f) { return f->return_cpsr; }
/**
 * @brief Frame pointer (r11) of a frame.
 *
 * @param[in] f Frame.
 * @return The saved value.
 */
static __always_inline Register ArchFrameFp(const CpuState *f) { return f->r[11]; }
/**
 * @brief Syscall number of a frame, passed in r12.
 *
 * @param[in] f Frame of an SVC.
 * @return The syscall number.
 */
static __always_inline uint32_t ArchFrameSvcNum(const CpuState *f) { return (uint32_t)f->r[12]; }

/**
 * @brief Reads the current frame pointer (r11).
 *
 * @return The current fp.
 */
static inline Register ArchCurrentFramePointer(void)
{
    Register fp;
    __asm__ volatile("mov %0, r11" : "=r"(fp));
    return fp;
}

/**
 * @brief Reads the current CPSR.
 *
 * @return The current CPSR.
 */
static inline Register ArchCurrentFlags(void)
{
    Register cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}

/** @brief Number of general-purpose registers in TaskRegs (r0-r12). */
#define ARCH_NUM_GP_REGS 13

/** @brief A task's user registers, as TaskGetRegs() returns and TaskSetRegs() takes them. */
typedef struct {
    Register gp[ARCH_NUM_GP_REGS]; /**< r0-r12. */
    Register sp;                   /**< User sp. */
    Register lr;                   /**< User lr. */
    Register pc;                   /**< Address the task resumes at. */
    Register flags;                /**< CPSR; only ARM_CPSR_USER_BITS can be set. */
} TaskRegs;

/** @brief CPSR mode field for user mode. */
#define ARM_CPSR_MODE_USR 0x10u
/** @brief CPSR Thumb state bit. */
#define ARM_CPSR_T (1u << 5)

/** @brief CPSR bits user code may set: N Z C V Q, IT, GE, E and T. */
#define ARM_CPSR_USER_BITS (0xF8000000u | 0x06000000u | 0x000F0000u | 0x0000FC00u | (1u << 9) | \
                            ARM_CPSR_T)

/**
 * @brief Copies a frame into the user register view.
 *
 * @param[in]  f   Frame.
 * @param[out] out Registers.
 */
static inline void ArchFrameToRegs(const CpuState *f, TaskRegs *out)
{
    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
        out->gp[i] = f->r[i];
    out->sp = f->sp_usr;
    out->lr = f->lr_usr;
    out->pc = f->return_pc;
    out->flags = f->return_cpsr;
}

/**
 * @brief Copies user registers into a frame, keeping the task in user mode.
 *
 * Bits outside ARM_CPSR_USER_BITS are dropped and the mode is forced to user.
 *
 * @param[out] f  Frame to write.
 * @param[in]  in Registers.
 * @return false, leaving @p f unchanged, if pc is misaligned for the requested ARM or Thumb state.
 */
static inline bool ArchRegsToFrame(CpuState *f, const TaskRegs *in)
{
    uint32_t cpsr = ((uint32_t)in->flags & ARM_CPSR_USER_BITS) | ARM_CPSR_MODE_USR;
    uint32_t align = (cpsr & ARM_CPSR_T) ? 1u : 3u;
    if ((uint32_t)in->pc & align)
        return false;

    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
        f->r[i] = in->gp[i];
    f->sp_usr = in->sp;
    f->lr_usr = in->lr;
    f->return_pc = in->pc;
    f->return_cpsr = (Register)cpsr;
    return true;
}

/**
 * @brief Names the processor mode in a CPSR.
 *
 * @param[in] cpsr CPSR value.
 * @return "USR", "SVC", "IRQ" and so on, or "???".
 */
static inline const char *arm_cpsr_mode_name(uint32_t cpsr)
{
    switch (cpsr & 0x1Fu) {
    case 0x10u:
        return "USR";
    case 0x11u:
        return "FIQ";
    case 0x12u:
        return "IRQ";
    case 0x13u:
        return "SVC";
    case 0x1Fu:
        return "SYS";
    case 0x16u:
        return "MON";
    case 0x17u:
        return "ABT";
    case 0x1Au:
        return "HYP";
    case 0x1Bu:
        return "UND";
    default:
        return "???";
    }
}

/**
 * @brief Formats a CPSR for logs, e.g. "[SVC ARM irq=dis fiq=en nZCv]".
 *
 * @param[out] buf   Output buffer.
 * @param[in]  bufsz Size of @p buf.
 * @param[in]  flags CPSR value.
 */
static inline void ArchFlagsDecode(char *buf, size_t bufsz, Register flags)
{
    uint32_t cpsr = (uint32_t)flags;
    (void)snprintf(buf, bufsz, "[%s %s irq=%s fiq=%s %c%c%c%c]", arm_cpsr_mode_name(cpsr),
                   (cpsr & (1u << 5)) ? "Thumb" : "ARM", (cpsr & (1u << 7)) ? "dis" : "en",
                   (cpsr & (1u << 6)) ? "dis" : "en", (cpsr >> 31) & 1u ? 'N' : 'n',
                   (cpsr >> 30) & 1u ? 'Z' : 'z', (cpsr >> 29) & 1u ? 'C' : 'c',
                   (cpsr >> 28) & 1u ? 'V' : 'v');
}

/**
 * @brief Tells whether a CPSR is in IRQ mode.
 *
 * @param[in] flags CPSR value.
 * @return true in IRQ mode.
 */
static inline bool ArchFlagsInIrqContext(Register flags)
{
    return ((uint32_t)flags & 0x1Fu) == 0x12u;
}

#endif // ARCH_ARM_IMPL_REGS_H
