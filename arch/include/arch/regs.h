// arch/regs.h - Neutral saved-register contract.
//
// arch_regs_t is the saved trap/exception frame. Neutral code never touches its
// fields directly; it uses the accessors below (implemented per-arch). reg_t is
// the architecture's natural register-width integer.
//
//   reg_t *arch_reg(arch_regs_t *f, unsigned i);  -- syscall ABI slot i (r/w)
//   reg_t  arch_regs_pc(const arch_regs_t *f);     -- saved return PC
//   reg_t  arch_regs_sp(const arch_regs_t *f);     -- saved user SP
//   reg_t  arch_regs_lr(const arch_regs_t *f);     -- saved user LR
//   reg_t  arch_regs_flags(const arch_regs_t *f);  -- saved status/flags

#ifndef ZUZU_ARCH_REGS_H
#define ZUZU_ARCH_REGS_H

#include <arch_impl/regs.h>   /* arch_regs_t + accessors (cpu_context_t for arch use) */

#endif // ZUZU_ARCH_REGS_H
