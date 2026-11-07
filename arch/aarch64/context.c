/**
 * @file context.c
 * @brief AArch64 task context function implementations
 */

#include <arch/context.h>
#include <arch/regs.h>
#include <string.h>

/**
 * @brief Where a new user task first runs; the saved lr of its initial CpuContext.
 */
extern void task_entry_trampoline(void);

CpuState *ArchUserFrame(uintptr_t kstack_top)
{
    return (CpuState *)(kstack_top - sizeof(CpuState));
}

void *ArchTaskUserInit(void *kstack_top, uintptr_t entry, uintptr_t user_sp, uintptr_t user_lr,
                       Register a0, Register a1)
{
    CpuState *f = ArchUserFrame((uintptr_t)kstack_top);
    uintptr_t sp = (uintptr_t)f;
    memset(f, 0, sizeof(*f));
    *ArchGetFromFrame(f, 0) = a0;
    *ArchGetFromFrame(f, 1) = a1;
    f->sp_el0 = (Register)user_sp;
    f->x[30] = (Register)user_lr;
    f->return_pc = (Register)entry;
    f->return_pstate = 0;

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
