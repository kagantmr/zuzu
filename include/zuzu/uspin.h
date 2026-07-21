#ifndef ZUZU_USPIN_H
#define ZUZU_USPIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * zzuspin - a minimal zuzu userspace spin lock for ARMv7-A.
 *
 * A test-and-set lock built on the exclusive monitor (LDREX/STREX). It is
 * meant for VERY short, non-blocking sections only -- a handful of
 * instructions guarding a shared word. It must never be held across a
 * syscall or a blocking IPC: a thread that sleeps while holding it stalls
 * every other contender until it is rescheduled.
 *
 * The asm avoids IT blocks so the same source assembles in ARM and Thumb.
 */
typedef volatile uint32_t zzuspin_t;   /* 0 = free, 1 = held */
#define ZZUSPIN_INIT 0u

static inline void zzuspin_lock(zzuspin_t *lk)
{
    uint32_t old, res;
    for (;;) {
        __asm__ volatile("ldrex %0, [%1]" : "=r"(old) : "r"(lk) : "memory");
        if (old != 0u)
            continue;                  /* already held; re-read */
        __asm__ volatile("strex %0, %2, [%1]"
                         : "=&r"(res) : "r"(lk), "r"(1u) : "memory");
        if (res == 0u)
            break;                     /* won the store; we own it */
    }
    __asm__ volatile("dmb ish" ::: "memory");
}

static inline void zzuspin_unlock(zzuspin_t *lk)
{
    __asm__ volatile("dmb ish" ::: "memory");
    *lk = 0u;
    __asm__ volatile("dsb ish\n\tsev" ::: "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_USPIN_H */
