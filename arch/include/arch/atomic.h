// arch/atomic.h - Neutral atomic-primitive contract.
//
//   uint32_t arch_ldrex(volatile uint32_t *);
//   uint32_t arch_strex(volatile uint32_t *, uint32_t);
//   void     arch_clrex(void);
//   int      atomic_cas(volatile uint32_t *, uint32_t expected, uint32_t desired);

#ifndef ZUZU_ARCH_ATOMIC_H
#define ZUZU_ARCH_ATOMIC_H

#include <arch_impl/atomic.h>

#endif // ZUZU_ARCH_ATOMIC_H
