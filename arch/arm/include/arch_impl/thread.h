#ifndef ARCH_ARM_IMPL_THREAD_H
#define ARCH_ARM_IMPL_THREAD_H

#include "kernel/task/task.h"

static inline void ArchSetTlsPointer(TaskObject *t)
{
    if (!t)
        return;

    __asm__ volatile("mcr p15, 0, %0, c13, c0, 3" ::"r"(t->task_info_va) : "memory");
}

#endif // ARCH_ARM_IMPL_THREAD_H
