// arch_impl/barrier.h - ARM memory barrier / event primitives (architecture-private).
//
// Do not include directly from neutral code; include <arch/barrier.h> instead.

#ifndef ZUZU_ARM_IMPL_BARRIER_H
#define ZUZU_ARM_IMPL_BARRIER_H

/**
 * Data Memory Barrier (DMB): all explicit memory accesses before the DMB are
 * globally observed before any after it. Used to order device I/O and shared
 * memory between processors.
 */
static inline void ArchDmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }

/**
 * Data Synchronization Barrier (DSB): blocks until all explicit memory
 * accesses before it have completed, not just been ordered.
 */
static inline void ArchDsb(void) { __asm__ volatile("dsb ish" ::: "memory"); }

/**
 * Instruction Synchronization Barrier (ISB): blocks until all explicit fetch
 * accesses before it have completed, not just been ordered.
 */
static inline void ArchIsb(void) { __asm__ volatile("isb" ::: "memory"); }

/**
 * Send Event (SEV): wakes cores blocked in WFE. Paired with a preceding DSB
 * so the state change that triggered the wakeup is visible before waking.
 */
static inline void ArchSev(void) { __asm__ volatile("sev" ::: "memory"); }

#endif // ZUZU_ARM_IMPL_BARRIER_H
