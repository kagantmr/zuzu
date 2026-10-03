// arch/regs.h - Neutral saved-register contract.
//
// CpuState is the saved trap/exception frame. Neutral code never touches its
// fields directly; it uses the accessors below (implemented per-arch). Register is
// the architecture's natural register-width integer.
//
//   Register *ArchGetFromFrame(CpuState *f, unsigned i);  -- syscall ABI slot i (r/w)
//   Register  arch_regs_pc(const CpuState *f);     -- saved return PC
//   Register  arch_regs_sp(const CpuState *f);     -- saved user SP
//   Register  arch_regs_lr(const CpuState *f);     -- saved user LR
//   Register  arch_regs_flags(const CpuState *f);  -- saved status/flags
//   Register  arch_regs_fp(const CpuState *f);      -- saved frame pointer (for backtrace)
//
// The two below read live CPU state (not a saved frame) — for diagnostics
// (e.g. core/panic.c) that need "where are we right now" rather than "where
// did we trap from".
//
//   Register  arch_current_fp(void);                  -- live frame-pointer register
//   Register  arch_current_flags(void);                -- live status/flags register
//
// Diagnostics-only helpers below (core/panic.c is the only caller); these let
// a panic dump stay free of any architecture-specific flag/mode encoding.
//
//   ARCH_NUM_GP_REGS                                   -- count of ArchGetFromFrame() slots
//   void arch_flags_decode(char *buf, size_t bufsz, Register flags);
//                                                       -- human-readable mode/flag string
//   bool arch_flags_in_irq_context(Register flags);    -- true if flags denotes IRQ context

#ifndef ZUZU_ARCH_REGS_H
#define ZUZU_ARCH_REGS_H

#include <arch_impl/regs.h>   /* CpuState + accessors (CpuContext for arch use) */

static __always_inline void ArchSetInFrame(CpuState *f, unsigned i, int value)
{
    *ArchGetFromFrame(f, i) = (Register)value;
} 

#endif // ZUZU_ARCH_REGS_H
