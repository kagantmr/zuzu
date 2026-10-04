#include "svc.h"
#include "kernel/space/space.h"
#include "core/ensure.h"
#include <arch/regs.h>
#include <arch/context.h>
#include "kernel/task/task.h"
#include "kernel/sched/sched.h"

void SvcManageTask(CpuState *frame)
{
    Handle h = (Handle)(*ArchGetFromFrame(frame, 0));
    ManageTaskVerb verb = (ManageTaskVerb)(*ArchGetFromFrame(frame, 1));

    if (h == -1) {
        ENSURE_ERR(frame, verb == MNGTASK_SET_PRIORITY || verb == MNGTASK_SET_MAX_PRIO ||
                           verb == MNGTASK_SET_TIMESLICE, ERR_BADARG);

        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        switch (verb) {
        case MNGTASK_SET_PRIORITY:
            ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
            current_task->priority = val;
            break;
        case MNGTASK_SET_MAX_PRIO:
            ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
            current_task->max_prio = val;
            break;
        case MNGTASK_SET_TIMESLICE:
            current_task->time_slice = val;
            break;
        default:
            break;
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
        return;
    }

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, h);
    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, entry->type == HANDLE_TASK, ERR_BADTYPE);
    ENSURE_ERR(frame, entry->perms & PERM_CNTL, ERR_NOPERM);
    TaskObject *target = entry->task;
    ENSURE_ERR(frame, target, ERR_BADHANDLE);

    switch (verb)
    {
    case MNGTASK_START: {
        ENSURE_ERR(frame, target->state == FROZEN, ERR_BUSY);
        KickstartArgs kargs;
        ENSURE_ERR(frame, CopyFromUser(&kargs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(kargs)), ERR_BADPTR);

        target->kernel_sp = (uint32_t *)ArchTaskUserInit(
            (void *)target->kernel_stack_top, (VirtAddr)kargs.entry, (VirtAddr)kargs.sp, USER_ELF_BASE,
            kargs.r0, kargs.r1, &target->trap_frame);
        target->state = READY;
        SchedAdd(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_KILL: {
        ENSURE_ERR(frame, target != current_task, ERR_BADARG);
        /* Already dead: a second TaskTerminate would TaskDestroy it while it
         * may still sit on the destroy queue, and the last handle close would
         * then free it under the reaper. */
        if (target->state != ZOMBIE)
        {
            if (target->state == FAULTED)
                SpaceUnfreeze(target->owner);
            TaskTerminate(target, ERR_DEAD);
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_PRIORITY: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
        bool requeue = (target->state == READY);   /* only READY tasks are on a run queue */
        if (requeue)
            SchedRemoveRunQueue(target);
        target->priority = val;
        if (requeue)
            SchedAdd(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_MAX_PRIO: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
        target->max_prio = val;
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_TIMESLICE: {
        target->time_slice = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SUSPEND:
        ArchSetInFrame(frame, 0, ERR_NOSYS);
        break;

    case MNGTASK_RESUME: {
        ENSURE_ERR(frame, target->state == FAULTED, ERR_BADARG);
        SpaceUnfreeze(target->owner);
        target->state = READY;
        SchedAdd(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_GET_REGS: {
        ENSURE_ERR(frame, target->trap_frame, ERR_DEAD);
        Register regs[ARCH_NUM_GP_REGS];
        for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
            regs[i] = *ArchGetFromFrame(target->trap_frame, i);
        ENSURE_ERR(frame, CopyToUser((void *)(*ArchGetFromFrame(frame, 2)), regs, sizeof(regs)), ERR_BADPTR);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_REGS: {
        ENSURE_ERR(frame, target->trap_frame, ERR_DEAD);
        Register regs[ARCH_NUM_GP_REGS];
        ENSURE_ERR(frame, CopyFromUser(regs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(regs)), ERR_BADPTR);
        for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i++)
            ArchSetInFrame(target->trap_frame, i, (int)regs[i]);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        break;
    }
}