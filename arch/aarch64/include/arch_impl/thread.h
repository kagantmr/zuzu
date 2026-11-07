/**
 * @file
 * @brief AArch64 thread pointer write.
 */

#ifndef ARCH_AARCH64_IMPL_THREAD_H
#define ARCH_AARCH64_IMPL_THREAD_H

#include "kernel/task/task.h"
#include "sysreg.h"

/**
 * @brief Loads a task's info block address into TPIDRRO_EL0, where user code reads it.
 *
 * @param[in] t Task about to run; NULL leaves the register unchanged.
 */
static inline void ArchSetTlsPointer(TaskObject *t)
{
    if (!t)
        return;

    WRITE_SYSREG(tpidrro_el0, t->task_info_va);
}

#endif // ARCH_AARCH64_IMPL_THREAD_H
