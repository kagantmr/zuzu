// arch_impl/thread.h - ARM thread-pointer register access (architecture-private).
//
// Do not include directly from neutral code; include <arch/thread.h> instead.

#ifndef ZUZU_ARM_IMPL_THREAD_H
#define ZUZU_ARM_IMPL_THREAD_H

#include "kernel/proc/thread.h"
#include <stdint.h>

/**
 * arch_set_thread_ptr - publish the thread pointer to user-readable TPIDRURO.
 *
 * Writes the thread's info VA to the ARM TPIDRURO register (cp15, c13, c0, 3),
 * so user-mode can read it via mrc p15, 0, rt, c13, c0, 3 for TLS / thread IDs.
 */
static inline void arch_set_thread_ptr(thread_t *t)
{
	if (!t)
		return;

	__asm__ volatile("mcr p15, 0, %0, c13, c0, 3" :: "r"(t->thread_info_va) : "memory");
}

#endif // ZUZU_ARM_IMPL_THREAD_H
