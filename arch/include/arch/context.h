/**
 * @file context.h
 * @brief Task context function definitions
 */

#ifndef ARCH_CONTEXT_H
#define ARCH_CONTEXT_H

#include <arch/regs.h>
#include <stdint.h>

/**
 * @brief Initializes a task's execution context.
 * 
 * @param[in] kstack_top The kernel stack of the task.
 * @param[in] entry The entry point of the task.
 * @param[in] user_sp The initial stack pointer of the user program
 * @param[in] user_lr The initial link register of the user program
 * @param[in] a0 Initial first register value
 * @param[in] a1 Initial second register value
 * 
 * @return void* The user stack pointer after pushing the context.
 */
void *ArchTaskUserInit(void *kstack_top, uintptr_t entry, uintptr_t user_sp, uintptr_t user_lr,
                       uint32_t a0, uint32_t a1);

/**
 * @brief Arch-neutral accessor for a task's context.
 * 
 * @param[in] kstack_top Start of the kernel stack.
 * 
 * @return CpuState* The CPU state struct of the task.
 */
CpuState *ArchUserFrame(uintptr_t kstack_top);

/**
 * @brief Initialize a task's context that is running on the kernel
 * 
 * @param[in] kstack_top Start of the kernel stack.
 * @param[in] entry The entry point function.
 * 
 * @return void* The stack pointer after pushing the context.
 */
void *ArchTaskKernelInit(void *kstack_top, void (*entry)(void));

#endif // ARCH_CONTEXT_H
