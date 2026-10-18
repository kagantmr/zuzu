/**
 * @file context.c
 * @brief Task context function implementations
 */

#include <arch/context.h>
#include <arch/regs.h>
#include <string.h>

/**
 * @brief ARM user CPSR value.
 */
#define ARM_CPSR_USER 0x10u

/**
 * @brief Entry point of the idle task.
 */
extern void task_entry_trampoline(void);

CpuState *ArchUserFrame(uintptr_t kstack_top) { return (CpuState *)(kstack_top - sizeof(CpuState)); }

void *ArchTaskUserInit(void *kstack_top, uintptr_t entry, uintptr_t user_sp, uintptr_t user_lr,
                       uint32_t a0, uint32_t a1)
{
    CpuState *f = ArchUserFrame((uintptr_t)kstack_top);
    uintptr_t sp = (uintptr_t)f;
    memset(f, 0, sizeof(*f));
    *ArchGetFromFrame(f, 0) = (Register)a0;
    *ArchGetFromFrame(f, 1) = (Register)a1;
    f->sp_usr = (Register)user_sp;
    f->lr_usr = (Register)user_lr;
    f->return_pc = (Register)entry;
    f->return_cpsr = ARM_CPSR_USER;

    sp -= sizeof(CpuContext);
    CpuContext *ctx = (CpuContext *)sp;
    memset(ctx, 0, sizeof(*ctx));
    ctx->lr = (Register)task_entry_trampoline;

    return (void *)sp;
}

void *ArchTaskKernelInit(void *kstack_top, void (*entry)(void))
{
    uintptr_t sp = (uintptr_t)kstack_top;

    sp -= sizeof(CpuContext);
    CpuContext *ctx = (CpuContext *)sp;
    memset(ctx, 0, sizeof(*ctx));
    ctx->lr = (Register)entry;

    return (void *)sp;
}
