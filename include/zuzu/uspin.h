#ifndef ZUZU_USPIN_H
#define ZUZU_USPIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <arch/atomic.h>
#include <arch/barrier.h>
#include <zuzu/task.h>   /* zuzu_yield */

/*
 * zzuspin - a minimal zuzu userspace spin lock.
 *
 * A test-and-set lock built on the exclusive monitor (arch_ldrex/arch_strex).
 * It is meant for VERY short, non-blocking sections only -- a handful of
 * instructions guarding a shared word. It must never be held across a
 * syscall or a blocking IPC: a thread that sleeps while holding it stalls
 * every other contender until it is rescheduled.
 *
 * The word is _Atomic, not just volatile: under LTO/-Os the ordering must
 * not rest on the arch primitives' "memory" clobbers alone. A contender that
 * finds the lock held yields rather than spinning -- on a uniprocessor,
 * spinning while the (preempted) holder waits to run just burns the whole
 * quantum.
 */
/* TEMPORARY: Loaf backend only. Deleted at Prowl. */
typedef _Atomic uint32_t zzuspin_t;   /* 0 = free, 1 = held */
#define ZZUSPIN_INIT 0u

static inline void zzuspin_lock(zzuspin_t *lk)
{
    volatile uint32_t *word = (volatile uint32_t *)lk;
    for (;;) {
        uint32_t old = arch_ldrex(word);
        if (old != 0u) {
            zuzu_yield();              /* held; let the holder run */
            continue;
        }
        if (arch_strex(word, 1u) == 0u)
            break;                     /* won the store; we own it */
    }
    arch_dmb();
}

static inline void zzuspin_unlock(zzuspin_t *lk)
{
    arch_dmb();
    *lk = 0u;
    arch_dsb();
    arch_sev();
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_USPIN_H */
