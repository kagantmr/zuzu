#ifndef ARCH_ARM_IMPL_REGS_H
#define ARCH_ARM_IMPL_REGS_H

#include <compiler.h>
#include <snprintf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t Register;

typedef struct CpuStateStruct {
    Register r[13];
    Register sp_usr;
    Register lr_usr;
    Register return_pc;
    Register return_cpsr;
} CpuState;

typedef struct {
    Register r4, r5, r6, r7, r8, r9, r10, r11;
    Register lr;
} CpuContext;

static __always_inline Register *ArchGetFromFrame(CpuState *f, unsigned i) { return &f->r[i]; }

static __always_inline Register ArchFramePc(const CpuState *f) { return f->return_pc; }
static __always_inline Register ArchFrameSp(const CpuState *f) { return f->sp_usr; }
static __always_inline Register ArchFrameLr(const CpuState *f) { return f->lr_usr; }
static __always_inline Register ArchFrameFlags(const CpuState *f) { return f->return_cpsr; }
static __always_inline Register ArchFrameFp(const CpuState *f) { return f->r[11]; }
static __always_inline uint32_t ArchFrameSvcNum(const CpuState *f) { return (uint32_t)f->r[12]; }

static inline Register ArchCurrentFramePointer(void)
{
    Register fp;
    __asm__ volatile("mov %0, r11" : "=r"(fp));
    return fp;
}

static inline Register ArchCurrentFlags(void)
{
    Register cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}

#define ARCH_NUM_GP_REGS 13

typedef struct {
    Register gp[ARCH_NUM_GP_REGS];
    Register sp;
    Register lr;
    Register pc;
    Register flags;
} TaskRegs;

#define ARM_CPSR_MODE_USR 0x10u
#define ARM_CPSR_T (1u << 5)

#define ARM_CPSR_USER_BITS (0xF8000000u | 0x06000000u | 0x000F0000u | 0x0000FC00u | (1u << 9) | \
                            ARM_CPSR_T)

static inline void ArchFrameToRegs(const CpuState *f, TaskRegs *out)
{
    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
        out->gp[i] = f->r[i];
    out->sp = f->sp_usr;
    out->lr = f->lr_usr;
    out->pc = f->return_pc;
    out->flags = f->return_cpsr;
}

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

static inline void ArchFlagsDecode(char *buf, size_t bufsz, Register flags)
{
    uint32_t cpsr = (uint32_t)flags;
    (void)snprintf(buf, bufsz, "[%s %s irq=%s fiq=%s %c%c%c%c]", arm_cpsr_mode_name(cpsr),
                   (cpsr & (1u << 5)) ? "Thumb" : "ARM", (cpsr & (1u << 7)) ? "dis" : "en",
                   (cpsr & (1u << 6)) ? "dis" : "en", (cpsr >> 31) & 1u ? 'N' : 'n',
                   (cpsr >> 30) & 1u ? 'Z' : 'z', (cpsr >> 29) & 1u ? 'C' : 'c',
                   (cpsr >> 28) & 1u ? 'V' : 'v');
}

static inline bool ArchFlagsInIrqContext(Register flags)
{
    return ((uint32_t)flags & 0x1Fu) == 0x12u;
}

#endif // ARCH_ARM_IMPL_REGS_H
