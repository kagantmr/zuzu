/**
 * @file
 * @brief ARMv7-A memory barriers.
 */

#ifndef ARCH_ARM_IMPL_BARRIER_H
#define ARCH_ARM_IMPL_BARRIER_H

/** @brief Data memory barrier, inner shareable (dmb ish). */
static inline void ArchDmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }

/** @brief Data synchronization barrier, inner shareable (dsb ish). */
static inline void ArchDsb(void) { __asm__ volatile("dsb ish" ::: "memory"); }

/** @brief Instruction synchronization barrier (isb). */
static inline void ArchIsb(void) { __asm__ volatile("isb" ::: "memory"); }

/** @brief dsb ish followed by isb: completes memory and system-register effects. */
static inline void ArchSyncBarrier(void) { __asm__ volatile("dsb ish\n\tisb" ::: "memory"); }

/** @brief Data synchronization barrier, full system (dsb sy). */
static inline void ArchDsbSy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

#endif // ARCH_ARM_IMPL_BARRIER_H
