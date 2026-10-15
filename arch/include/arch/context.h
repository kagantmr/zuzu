// arch/context.h - Neutral thread bootstrap / switch-context contract.
//
// The kernel never constructs initial kernel-stack frames by hand; the arch
// layer builds them so that a newly created thread, when first scheduled,
// "returns" into the right place. The callee-saved switch context and any
// FP save area are arch-private details hidden behind these calls.

#ifndef ARCH_CONTEXT_H
#define ARCH_CONTEXT_H

#include <arch/regs.h>
#include <stdint.h>

/**
 * Build a fresh kernel stack for a thread that will drop into USER mode.
 *
 * @param kstack_top      Top of the thread's kernel stack.
 * @param entry           User entry point (PC).
 * @param user_sp         Initial user stack pointer.
 * @param user_lr         Return address if the user entry returns.
 * @param a0,a1           First two argument register values.
 * @return The initial kernel stack pointer (store in thread->kernel_sp).
 */
void *ArchTaskUserInit(void *kstack_top, uintptr_t entry, uintptr_t user_sp, uintptr_t user_lr,
                       uint32_t a0, uint32_t a1);

/**
 * The user-mode trap frame of a thread whose kernel stack tops out at kstack_top.
 * Every entry from user mode saves the user registers here, so the address is
 * fixed for the thread's whole life.
 */
CpuState *ArchUserFrame(uintptr_t kstack_top);

/**
 * Build a fresh kernel stack for a thread that begins in KERNEL mode at `entry`
 * (e.g. the idle thread). Returns the initial kernel stack pointer.
 */
void *ArchTaskKernelInit(void *kstack_top, void (*entry)(void));

#endif // ARCH_CONTEXT_H
