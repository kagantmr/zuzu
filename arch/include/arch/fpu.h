// arch/fpu.h - Neutral lazy-FPU contract.
//
// The FPU register file is expensive to save/restore, so the kernel keeps it
// lazily bound to whichever thread last used it (kernel/sched/sched.c tracks
// this as fpu_owner). On every reschedule away from the owner, FPU access is
// trapped off; the first FPU instruction any other thread executes raises an
// arch trap, which the arch exception handler turns into a save-of-owner /
// restore-of-current / retry sequence before handing control back.
//
//   typedef /* opaque */ FpuState;        -- one thread's saved FPU regs
//   void ArchFpuTrapDisable(void);        -- next FPU access traps
//   void ArchFpuEnableAccess(void);         -- FPU instructions run normally
//   void ArchFpuSaveState(FpuState *state);      -- save live FPU regs
//   void ArchFpuRestoreState(const FpuState *state); -- load live FPU regs

#ifndef ARCH_FPU_H
#define ARCH_FPU_H

#include <arch_impl/fpu.h>

#endif // ARCH_FPU_H
