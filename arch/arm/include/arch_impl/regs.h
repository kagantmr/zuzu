// arch_impl/regs.h - ARM saved-register layouts (architecture-private).
//
// Do not include directly from architecture-neutral code; include <arch/regs.h>
// instead. The struct layout below must match the stmfd/srs sequence in
// arch/arm/exceptions/entry.S exactly — the assembly writes these fields by
// offset.

#ifndef ZUZU_ARM_IMPL_REGS_H
#define ZUZU_ARM_IMPL_REGS_H

#include <compiler.h>
#include <snprintf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Natural register-width integer for this architecture (32-bit on ARMv7-A). */
typedef int32_t Register;

/**
 * Represents a process's saved CPU state at the time of an exception.
 * Layout must match the stmfd sequence in entry.S exactly,
 * the assembly writes directly into this struct by offset.
 */
typedef struct exception_frame {
    Register r[13];        /* r0-r12 */
    Register sp_usr;       /* user SP saved via SRS */
    Register lr_usr;       /* user LR saved via SRS */
    Register return_pc;    /* adjusted return address (LR - offset) */
    Register return_cpsr;  /* saved CPSR/SPSR value you return with */
} ExceptionFrame;

typedef struct cpu_context
{
    Register r4, r5, r6, r7, r8, r9, r10, r11;
    Register lr; // return address (or entry point for new process)
} CpuContext;

/* Neutral alias used by architecture-independent code. */
typedef struct exception_frame CpuState;

/* ---- Accessors (the neutral contract; see <arch/regs.h>) ----------------- */
/* Syscall ABI slots: arg i / return value i map to r[i] on ARM. Called many
 * times per syscall (every arg read, every return-value write); always_inline
 * guarantees the pointer arithmetic never survives as a real call even if a
 * caller is judged too large to inline into otherwise. */
static __always_inline Register *ArchGetFromFrame(CpuState *f, unsigned i) { return &f->r[i]; }

static __always_inline Register arch_regs_pc(const CpuState *f)    { return f->return_pc; }
static __always_inline Register arch_regs_sp(const CpuState *f)    { return f->sp_usr; }
static __always_inline Register arch_regs_lr(const CpuState *f)    { return f->lr_usr; }
static __always_inline Register arch_regs_flags(const CpuState *f) { return f->return_cpsr; }
static __always_inline Register arch_regs_fp(const CpuState *f)    { return f->r[11]; }

/* Live reads of current CPU state (see <arch/regs.h>). */
static inline Register arch_current_fp(void)
{
    Register fp;
    __asm__ volatile("mov %0, r11" : "=r"(fp));
    return fp;
}

static inline Register arch_current_flags(void)
{
    Register cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}

/* Count of ArchGetFromFrame() slots (r0-r12 on ARM) -- see <arch/regs.h>. */
#define ARCH_NUM_GP_REGS 13

static inline const char *arm_cpsr_mode_name(uint32_t cpsr)
{
    switch (cpsr & 0x1Fu)
    {
    case 0x10u: return "USR";
    case 0x11u: return "FIQ";
    case 0x12u: return "IRQ";
    case 0x13u: return "SVC";
    case 0x1Fu: return "SYS";
    case 0x16u: return "MON";
    case 0x17u: return "ABT";
    case 0x1Au: return "HYP";
    case 0x1Bu: return "UND";
    default:    return "???";
    }
}

/* Diagnostics-only (see <arch/regs.h>): mode + Thumb/IRQ/FIQ + NZCV string. */
static inline void arch_flags_decode(char *buf, size_t bufsz, Register flags)
{
    uint32_t cpsr = (uint32_t)flags;
    (void)snprintf(buf, bufsz, "[%s %s irq=%s fiq=%s %c%c%c%c]", arm_cpsr_mode_name(cpsr),
                   (cpsr & (1u << 5)) ? "Thumb" : "ARM", (cpsr & (1u << 7)) ? "dis" : "en",
                   (cpsr & (1u << 6)) ? "dis" : "en", (cpsr >> 31) & 1u ? 'N' : 'n',
                   (cpsr >> 30) & 1u ? 'Z' : 'z', (cpsr >> 29) & 1u ? 'C' : 'c',
                   (cpsr >> 28) & 1u ? 'V' : 'v');
}

/* Diagnostics-only (see <arch/regs.h>): true if flags denotes IRQ mode. */
static inline bool arch_flags_in_irq_context(Register flags)
{
    return ((uint32_t)flags & 0x1Fu) == 0x12u;
}

#endif // ZUZU_ARM_IMPL_REGS_H
