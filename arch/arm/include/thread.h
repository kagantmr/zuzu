#ifndef ZUZU_ARCH_THREAD_H
#define ZUZU_ARCH_THREAD_H

#include "kernel/proc/thread.h"
#include <stdint.h>

/**
 * arch_set_thread_ptr - Write thread pointer to TPIDRUR (User-accessible)
 * 
 * Writes the thread pointer to the ARM TPIDRUR register (cp15, 0, c13, c0, 2)
 * making it readable from user-mode via mrc p15, 0, rt, c13, c0, 2.
 * This allows user-space code to access thread-local storage and thread IDs.
 */
static inline void arch_set_thread_ptr(thread_t *t)
{
	if (!t)
		return;
	
	/* Write thread pointer to TPIDRUR (cp15, 0, c13, c0, 2)
	 * User-mode can read this via: mrc p15, 0, r0, c13, c0, 2
	 * This makes the thread pointer accessible to user-space code.
	 */
	__asm__ volatile("mcr p15, 0, %0, c13, c0, 2" ::"r"((uintptr_t)t) : "memory");
}

#endif