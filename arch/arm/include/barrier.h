#ifndef ARCH_BARRIER_H
#define ARCH_BARRIER_H

static inline void arch_dmb(void)
{
    __asm__ volatile("dmb ish\n" ::: "memory");
}

#endif