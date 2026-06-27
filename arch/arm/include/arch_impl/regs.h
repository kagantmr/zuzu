// arch_impl/regs.h - ARM saved-register layouts (architecture-private).
//
// Do not include directly from architecture-neutral code; include <arch/regs.h>
// instead. The struct layout below must match the stmfd/srs sequence in
// arch/arm/exceptions/entry.S exactly — the assembly writes these fields by
// offset.

#ifndef ZUZU_ARM_IMPL_REGS_H
#define ZUZU_ARM_IMPL_REGS_H

#include <stdint.h>

/**
 * Represents a process's saved CPU state at the time of an exception.
 * Layout must match the stmfd sequence in entry.S exactly,
 * the assembly writes directly into this struct by offset.
 */
typedef struct exception_frame {
    uint32_t r[13];        /* r0-r12 */
    uint32_t sp_usr;       /* user SP saved via SRS */
    uint32_t lr_usr;       /* user LR saved via SRS */
    uint32_t return_pc;    /* adjusted return address (LR - offset) */
    uint32_t return_cpsr;  /* saved CPSR/SPSR value you return with */
} exception_frame_t;

typedef struct cpu_context
{
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    uint32_t lr; // return address (or entry point for new process)
} cpu_context_t;

/* Neutral alias used by architecture-independent code. */
typedef struct exception_frame arch_regs_t;

/* ---- Accessors (the neutral contract; see <arch/regs.h>) ----------------- */
/* Syscall ABI slots: arg i / return value i map to r[i] on ARM. */
static inline uint32_t *arch_reg(arch_regs_t *f, unsigned i) { return &f->r[i]; }

static inline uint32_t arch_regs_pc(const arch_regs_t *f)    { return f->return_pc; }
static inline uint32_t arch_regs_sp(const arch_regs_t *f)    { return f->sp_usr; }
static inline uint32_t arch_regs_lr(const arch_regs_t *f)    { return f->lr_usr; }
static inline uint32_t arch_regs_flags(const arch_regs_t *f) { return f->return_cpsr; }

#endif // ZUZU_ARM_IMPL_REGS_H
