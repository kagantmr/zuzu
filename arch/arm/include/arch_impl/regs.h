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

#endif // ZUZU_ARM_IMPL_REGS_H
