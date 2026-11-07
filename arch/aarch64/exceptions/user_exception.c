/**
 * @file
 * @brief AArch64 user mode synchronous exception handling.
 */

#include "core/log.h"
#include "core/panic.h"
#include "esr.h"
#include "kernel/bench.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kernel/svc/svc.h"
#include "kernel/task/kstack.h"
#include "kernel/task/task.h"
#include "util/log.h"
#include <arch/fpu.h>
#include <arch/irq.h>
#include <arch/mmu.h>
#include <arch/regs.h>
#include <arch_impl/sysreg.h>
#include <stdint.h>

/**
 * @brief Logs why the running task dies, then kills it and schedules away.
 *
 * @param[in] frame  The task's registers.
 * @param[in] reason Kill reason handed to TaskFault().
 * @param[in] what   What the task did, for the log.
 * @param[in] esr    ESR_EL1 of the exception.
 */
static void KillCurrentTask(const CpuState *frame, KilledReason reason, const char *what,
                            uint64_t esr)
{
    SpaceObject *space = current_task->owner;
    KERROR("Oops! '%s' (SPID %d, TID %d) killed: %s (ESR=" REG_FMT ") @ pc=" REG_FMT,
           space ? space->name : "?", space ? space->spid : -1, current_task->tid, what,
           REG_ARG(esr), REG_ARG(frame->return_pc));
    TaskFault(current_task, reason);
    Schedule();
}

void UserSyncException(CpuState *frame);

void __hot UserSyncException(CpuState *frame)
{
    uint64_t esr = READ_SYSREG(esr_el1);
    EsrClass ec = EsrEc(esr);
    uint32_t iss = EsrIss(esr);

    SpaceObject *current_space = current_task ? current_task->owner : NULL;

    switch (ec) {
    case EC_SVC64: {
        if (unlikely((frame->return_pstate & 0xF) != 0))
            return;
        SvcDispatch((uint32_t)(esr & 0xFFFF), frame);
        return;
    };
    case EC_FP_SIMD:
        ArchFpuEnableAccess();
        fpu_access_enabled = true;
        if (fpu_owner != current_task) {
            if (fpu_owner)
                FpuSaveState(&fpu_owner->fpu_state);
            FpuRestoreState(&current_task->fpu_state);
            fpu_owner = current_task;
        }
        break;
    case EC_UNKNOWN:
        KillCurrentTask(frame, KILLED_FAULT_UNDEF, "undefined instruction", esr);
        break;
    case EC_DABT_LOWER:
    case EC_IABT_LOWER: {
        VirtAddr far = (VirtAddr)READ_SYSREG(far_el1);
        bool is_data = ec == EC_DABT_LOWER;
        bool write = is_data && EsrIsWrite(iss);
        if (EsrIsTranslationFault(iss) && VmmDemandPage(current_space->as, far, write))
            return;
        KERROR("Oops! '%s' (SPID %d, TID %d) killed: segmentation fault, %s @ " REG_FMT
               " pc=" REG_FMT " (%s)",
               current_space->name, current_space->spid, current_task->tid,
               is_data ? (write ? "write" : "read") : "execute", REG_ARG(far),
               REG_ARG(frame->return_pc), DecodeDfsc(iss & 0x3F));
        TaskFault(current_task, is_data ? KILLED_FAULT_DATA : KILLED_FAULT_PREFETCH);
        Schedule();
        break;
    }
    case EC_PC_ALIGN:
        KillCurrentTask(frame, KILLED_FAULT_ALIGN, "PC alignment fault", esr);
        break;
    case EC_SP_ALIGN:
        KillCurrentTask(frame, KILLED_FAULT_ALIGN, "SP alignment fault", esr);
        break;
    case EC_BRK64:
        KillCurrentTask(frame, KILLED_FAULT_UNDEF, "brk instruction", esr);
        break;
    case EC_WFX:
        KillCurrentTask(frame, KILLED_FAULT_UNDEF, "wfi/wfe in user mode", esr);
        break;
    default:
        KillCurrentTask(frame, KILLED_FAULT_UNDEF, EcName(ec), esr);
        break;
    }
}
