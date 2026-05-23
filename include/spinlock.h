#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

#ifdef __KERNEL__
#include "arch/arm/include/atomic.h"
#include <barrier.h>
#include "arch/arm/include/irq.h"

static inline void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags)
{
    *flags = arch_irq_save();
    do {
        while (arch_ldrex(&lock->locked) != 0)
            ;
    } while (arch_strex(&lock->locked, 1) != 0);
    arch_dmb();
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    arch_dmb();
    lock->locked = 0;
    arch_irq_restore(flags);
}

static inline void spin_lock(spinlock_t *lock)
{
    do {
        while (arch_ldrex(&lock->locked) != 0)
            ;
    } while (arch_strex(&lock->locked, 1) != 0);
    arch_dmb();
}

static inline void spin_unlock(spinlock_t *lock)
{
    arch_dmb();
    lock->locked = 0;
}

#else

static inline void spin_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        while (lock->locked);
}

static inline void spin_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

#endif
#endif  // SPINLOCK_H