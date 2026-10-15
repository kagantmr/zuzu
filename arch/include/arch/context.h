#ifndef ARCH_CONTEXT_H
#define ARCH_CONTEXT_H

#include <arch/regs.h>
#include <stdint.h>

void *ArchTaskUserInit(void *kstack_top, uintptr_t entry, uintptr_t user_sp, uintptr_t user_lr,
                       uint32_t a0, uint32_t a1);

CpuState *ArchUserFrame(uintptr_t kstack_top);

void *ArchTaskKernelInit(void *kstack_top, void (*entry)(void));

#endif // ARCH_CONTEXT_H
