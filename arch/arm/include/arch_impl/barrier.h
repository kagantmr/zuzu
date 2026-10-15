#ifndef ARCH_ARM_IMPL_BARRIER_H
#define ARCH_ARM_IMPL_BARRIER_H

static inline void ArchDmb(void) { __asm__ volatile("dmb ish" ::: "memory"); }

static inline void ArchDsb(void) { __asm__ volatile("dsb ish" ::: "memory"); }

static inline void ArchIsb(void) { __asm__ volatile("isb" ::: "memory"); }

static inline void ArchSyncBarrier(void) { __asm__ volatile("dsb ish\n\tisb" ::: "memory"); }

static inline void ArchDsbSy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

#endif // ARCH_ARM_IMPL_BARRIER_H
