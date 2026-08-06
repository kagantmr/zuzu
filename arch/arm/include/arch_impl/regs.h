// arch_impl/regs.h - ARM saved-register layouts (architecture-private).
//
// Do not include directly from architecture-neutral code; include <arch/regs.h>
// instead. The struct layout below must match the stmfd/srs sequence in
// arch/arm/exceptions/entry.S exactly — the assembly writes these fields by
// offset.

#ifndef ZUZU_ARM_IMPL_REGS_H
#define ZUZU_ARM_IMPL_REGS_H

#include <compiler.h>
#include <stdint.h>

/* Natural register-width integer for this architecture (32-bit on ARMv7-A). */
typedef uint32_t Register;

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
static __always_inline Register *arch_reg(CpuState *f, unsigned i) { return &f->r[i]; }

static __always_inline Register arch_regs_pc(const CpuState *f)    { return f->return_pc; }
static __always_inline Register arch_regs_sp(const CpuState *f)    { return f->sp_usr; }
static __always_inline Register arch_regs_lr(const CpuState *f)    { return f->lr_usr; }
static __always_inline Register arch_regs_flags(const CpuState *f) { return f->return_cpsr; }

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

#endif // ZUZU_ARM_IMPL_REGS_H
