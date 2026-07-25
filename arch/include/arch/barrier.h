// arch/barrier.h - Neutral memory-barrier / event contract.
//
//   void arch_dmb(void);  -- data memory barrier (order accesses)
//   void arch_dsb(void);  -- data synchronization barrier (complete accesses)
//   void arch_sev(void);  -- send event, wake cores blocked in WFE

#ifndef ZUZU_ARCH_BARRIER_H
#define ZUZU_ARCH_BARRIER_H

#include <arch_impl/barrier.h>

#endif // ZUZU_ARCH_BARRIER_H
