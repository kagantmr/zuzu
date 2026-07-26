// arch/fpu.h - Neutral lazy-FPU contract.
//
// The FPU register file is expensive to save/restore, so the kernel keeps it
// lazily bound to whichever thread last used it (kernel/sched/sched.c tracks
// this as fpu_owner). On every reschedule away from the owner, FPU access is
// trapped off; the first FPU instruction any other thread executes raises an
// arch trap, which the arch exception handler turns into a save-of-owner /
// restore-of-current / retry sequence before handing control back.
//
//   typedef /* opaque */ fpu_state_t;        -- one thread's saved FPU regs
//   void arch_fpu_trap_disable(void);        -- next FPU access traps
//   void arch_fpu_trap_enable(void);         -- FPU instructions run normally
//   void arch_fpu_save(fpu_state_t *state);      -- save live FPU regs
//   void arch_fpu_restore(const fpu_state_t *state); -- load live FPU regs

#ifndef ZUZU_ARCH_FPU_H
#define ZUZU_ARCH_FPU_H

#include <arch_impl/fpu.h>

#endif // ZUZU_ARCH_FPU_H
