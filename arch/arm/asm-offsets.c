#include "kernel/task/task.h"
#include <stddef.h>

#define DEFINE(sym, val) __asm__ volatile("\n.ascii \"->" #sym " %c0\"" ::"i"(val))

void AsmOffsets(void);
void AsmOffsets(void)
{
    DEFINE(TASK_KERNEL_SP, offsetof(TaskObject, kernel_sp));
}
