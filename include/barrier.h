// barrier.h - ARM memory barrier definitions

#ifndef ARCH_BARRIER_H
#define ARCH_BARRIER_H

/**
 * @brief Data Memory Barrier (DMB) - ensures that all explicit memory accesses before the DMB are globally observed before any after the DMB.
 * This is used to ensure ordering of memory operations, especially in the context of device I/O or shared memory between processors.
 * The processor is not allowed to order instructions across this barrier.
 */
static inline void arch_dmb(void)
{
    __asm__ volatile("dmb ish\n" ::: "memory");
}

#endif