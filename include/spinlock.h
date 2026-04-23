#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>
#include "arch/arm/include/atomic.h"
#include "arch/arm/include/barrier.h"
#include "arch/arm/include/irq.h"

typedef struct {
    volatile uint32_t locked;  // 0 = free, 1 = held
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

static inline void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags)
{
    *flags = arch_irq_save();
    do {
        while (arch_ldrex(&lock->locked) != 0)
            ;   /* no-op on single core, wfe loop on multi core */
    } while (arch_strex(&lock->locked, 1) != 0);
    arch_dmb();
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    arch_dmb();
    lock->locked = 0;
    arch_irq_restore(flags);
}

#endif  // SPINLOCK_H