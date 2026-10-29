/**
 * @file
 * @brief ARMv7-A thread pointer write.
 */

#ifndef ARCH_ARM_IMPL_THREAD_H
#define ARCH_ARM_IMPL_THREAD_H

#include "kernel/task/task.h"

/**
 * @brief Loads a task's info block address into TPIDRURO, where user code reads it.
 *
 * @param[in] t Task about to run; NULL leaves the register unchanged.
 */
static inline void ArchSetTlsPointer(TaskObject *t)
{
    if (!t)
        return;

    __asm__ volatile("mcr p15, 0, %0, c13, c0, 3" ::"r"(t->task_info_va) : "memory");
}

#endif // ARCH_ARM_IMPL_THREAD_H
