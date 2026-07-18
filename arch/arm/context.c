// context.c - ARM thread bootstrap (initial kernel-stack construction).
//
// Builds the kernel-stack layout that the context switch (switch.S) and the
// user-entry trampoline (entry.S) expect for a freshly created thread:
//
//   higher addr  ┌─────────────────────────┐  kstack_top
//                │ exception frame (arch_regs_t) │  (user-entry threads only)
//                ├─────────────────────────┤
//                │ switch context (cpu_context_t) │  lr = trampoline / entry
//                ├─────────────────────────┤
//                │ VFP/FP save area        │
//   lower addr   └─────────────────────────┘  <- returned kernel_sp

#include <arch/context.h>
#include <arch/regs.h>
#include <mem.h>

/* Initial CPSR for a user thread: USR mode (0x10), IRQs enabled. */
#define ARM_CPSR_USER     0x10u
/* Bytes reserved on the kernel stack for the VFP/FP register save area:
 * d0-d31 (32 doubles) + FPSCR, matching the switch.S save/restore layout. */
#define ARM_VFP_SAVE_SIZE 260u

/* Entry trampoline that pops the exception frame and returns to user mode. */
extern void process_entry_trampoline(void);

void *arch_thread_user_init(void *kstack_top, uintptr_t entry, uintptr_t user_sp,
                            uintptr_t user_lr, uint32_t a0, uint32_t a1,
                            arch_regs_t **trap_frame_out)
{
    uintptr_t sp = (uintptr_t)kstack_top;

    sp -= sizeof(arch_regs_t);
    arch_regs_t *f = (arch_regs_t *)sp;
    memset(f, 0, sizeof(*f));
    *arch_reg(f, 0) = a0;
    *arch_reg(f, 1) = a1;
    f->sp_usr       = (uint32_t)user_sp;
    f->lr_usr       = (uint32_t)user_lr;
    f->return_pc    = (uint32_t)entry;
    f->return_cpsr  = ARM_CPSR_USER;
    if (trap_frame_out)
        *trap_frame_out = f;

    sp -= sizeof(cpu_context_t);
    cpu_context_t *ctx = (cpu_context_t *)sp;
    memset(ctx, 0, sizeof(*ctx));
    ctx->lr = (uint32_t)process_entry_trampoline;

    sp -= ARM_VFP_SAVE_SIZE;
    memset((void *)sp, 0, ARM_VFP_SAVE_SIZE);

    return (void *)sp;
}

void *arch_thread_kernel_init(void *kstack_top, void (*entry)(void))
{
    uintptr_t sp = (uintptr_t)kstack_top;

    sp -= sizeof(cpu_context_t);
    cpu_context_t *ctx = (cpu_context_t *)sp;
    memset(ctx, 0, sizeof(*ctx));
    ctx->lr = (uint32_t)entry;

    sp -= ARM_VFP_SAVE_SIZE;
    memset((void *)sp, 0, ARM_VFP_SAVE_SIZE);

    return (void *)sp;
}
