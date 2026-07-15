#ifndef SPINLOCK_H
#define SPINLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    volatile uint32_t locked; // 0 if unlocked, 1 if locked
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

#ifdef __KERNEL__
#include <arch/atomic.h>
#include <barrier.h>
#include <arch/cpu.h>

/**
 * @brief Acquires the spinlock and saves the current interrupt state.
 * 
 * @param lock Pointer to the spinlock to acquire.
 * @param flags Pointer to a variable where the current interrupt state will be saved.
 */
static inline void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags)
{
    *flags = arch_irq_save();
    do {
        while (arch_ldrex(&lock->locked) != 0)
            ;
    } while (arch_strex(&lock->locked, 1) != 0);
    arch_dmb();
}

/**
 * @brief Releases the spinlock and restores the previous interrupt state.
 * 
 * @param lock Pointer to the spinlock to release.
 * @param flags The saved interrupt state to restore.
 */
static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    arch_dmb();
    lock->locked = 0;
    arch_irq_restore(flags);
}

/**
 * @brief Acquires the spinlock without saving the interrupt state.
 * 
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void spin_lock(spinlock_t *lock)
{
    do {
        while (arch_ldrex(&lock->locked) != 0)
            ;
    } while (arch_strex(&lock->locked, 1) != 0);
    arch_dmb();
}

/**
 * @brief Releases the spinlock without restoring the interrupt state.
 * 
 * @param lock Pointer to the spinlock to release.
 */
static inline void spin_unlock(spinlock_t *lock)
{
    arch_dmb();
    lock->locked = 0;
}

#else

/**
 * @brief Acquires the spinlock using atomic operations.
 * 
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void spin_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        while (lock->locked);
}

/**
 * @brief Releases the spinlock using atomic operations.
 * 
 * @param lock Pointer to the spinlock to release.
 */
static inline void spin_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

#endif

#ifdef __cplusplus
}
#endif

#endif  // SPINLOCK_H