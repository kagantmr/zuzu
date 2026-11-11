/**
 * @file svc_managetask.c
 * @brief Implementation of the ManageTask() svc.
 */

#include "core/ensure.h"
#include "kernel/clock.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kernel/task/task.h"
#include "svc.h"
#include <arch/context.h>
#include <arch/regs.h>

/**
 * @brief Set a task's time slice.
 *
 * @param[in,out] t The task.
 * @param[in] us The slice in microseconds, already checked against its range and the task's maximum.
 */
static void TaskSetSlice(TaskObject *t, uint32_t us)
{
    t->time_slice_us = us;
    t->time_slice_counts = ClockUsToCounts(us);
}

/**
 * @brief Set the longest time slice a task may set, shortening its current slice if needed.
 *
 * @param[in,out] t The task.
 * @param[in] us The maximum in microseconds, already checked.
 */
static void TaskSetMaxSlice(TaskObject *t, uint32_t us)
{
    t->max_time_slice_us = us;
    if (t->time_slice_us > us)
        TaskSetSlice(t, us);
}

void SvcManageTask(CpuState *frame)
{
    Handle h = (Handle)(*ArchGetFromFrame(frame, 0));
    ManageTaskVerb verb = (ManageTaskVerb)(*ArchGetFromFrame(frame, 1));

    if (h == -1) {
        ENSURE_ERR(frame,
                   verb == MNGTASK_SET_PRIORITY || verb == MNGTASK_SET_MAX_PRIO ||
                       verb == MNGTASK_SET_TIMESLICE || verb == MNGTASK_SET_MAX_TIMESLICE,
                   ERR_BADARG);

        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        switch (verb) {
        case MNGTASK_SET_PRIORITY:
            ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
            current_task->base_prio = val;
            TaskRecomputePriority(current_task);
            TaskSyncTcbPrio(current_task);
            break;
        case MNGTASK_SET_MAX_PRIO:
            ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
            current_task->max_prio = val;
            if (current_task->base_prio > val) {
                current_task->base_prio = val;
                TaskRecomputePriority(current_task);
            }
            TaskSyncTcbPrio(current_task);
            break;
        case MNGTASK_SET_TIMESLICE:
            ENSURE_ERR(frame, val >= TIMESLICE_MIN_US && val <= TIMESLICE_MAX_US, ERR_BADARG);
            ENSURE_ERR(frame, val <= current_task->max_time_slice_us, ERR_NOPERM);
            TaskSetSlice(current_task, val);
            break;
        case MNGTASK_SET_MAX_TIMESLICE:
            ENSURE_ERR(frame, val >= TIMESLICE_MIN_US && val <= TIMESLICE_MAX_US, ERR_BADARG);
            ENSURE_ERR(frame, val <= current_task->max_time_slice_us, ERR_NOPERM);
            TaskSetMaxSlice(current_task, val);
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

    switch (verb) {
    case MNGTASK_START: {
        ENSURE_ERR(frame, target->state == TASK_STATE_FROZEN, ERR_BUSY);
        KickstartArgs kargs;
        ENSURE_ERR(frame,
                   CopyFromUser(&kargs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(kargs)),
                   ERR_BADPTR);

        target->kernel_sp = (uint32_t *)ArchTaskUserInit(
            (void *)target->kernel_stack_top, (VirtAddr)kargs.entry, (VirtAddr)kargs.sp,
            USER_ELF_BASE, kargs.r0, kargs.r1);
        target->state = TASK_STATE_READY;
        SchedAdd(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_KILL: {
        ENSURE_ERR(frame, target != current_task, ERR_BADARG);
        
        if (target->state != TASK_STATE_ZOMBIE) {
            if (target->state == TASK_STATE_FAULTED)
                SpaceFaultCleared(target->owner);
            TaskTerminate(target, ERR_DEAD);
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_PRIORITY: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val <= current_task->max_prio && val <= target->max_prio, ERR_NOPERM);
        target->base_prio = val;
        TaskRecomputePriority(target);
        TaskSyncTcbPrio(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_MAX_PRIO: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val <= current_task->max_prio, ERR_NOPERM);
        target->max_prio = val;
        if (target->base_prio > target->max_prio) {
            target->base_prio = target->max_prio;
            TaskRecomputePriority(target);
        }
        TaskSyncTcbPrio(target);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_TIMESLICE: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val >= TIMESLICE_MIN_US && val <= TIMESLICE_MAX_US, ERR_BADARG);
        ENSURE_ERR(frame, val <= target->max_time_slice_us, ERR_NOPERM);
        TaskSetSlice(target, val);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_MAX_TIMESLICE: {
        uint32_t val = (uint32_t)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, val >= TIMESLICE_MIN_US && val <= TIMESLICE_MAX_US, ERR_BADARG);
        ENSURE_ERR(frame, val <= current_task->max_time_slice_us, ERR_NOPERM);
        TaskSetMaxSlice(target, val);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SUSPEND: {
        ENSURE_ERR(frame, target->state != TASK_STATE_ZOMBIE, ERR_DEAD);
        ENSURE_ERR(frame,
                   target->state != TASK_STATE_FROZEN && target->state != TASK_STATE_FAULTED,
                   ERR_BADARG);
        ArchSetInFrame(frame, 0, ZUZU_OK);
        if (target->suspended)
            break;
        target->suspended = true;
        if (target->state == TASK_STATE_READY)
            SchedRemoveRunQueue(target);
        if (target == current_task)
            Schedule();
    } break;

    case MNGTASK_RESUME: {
        if (target->state == TASK_STATE_FAULTED) {
            target->state = TASK_STATE_READY;
            SpaceFaultCleared(target->owner);
            SchedAdd(target);
        } else {
            ENSURE_ERR(frame, target->suspended, ERR_BADARG);
            target->suspended = false;
            if (target->state == TASK_STATE_READY && !target->node.next)
                SchedAdd(target);
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_GET_REGS: {
        ENSURE_ERR(frame, target->state != TASK_STATE_ZOMBIE, ERR_DEAD);
        ENSURE_ERR(frame, target->state == TASK_STATE_FAULTED || target->suspended, ERR_BUSY);
        TaskRegs regs;
        ArchFrameToRegs(target->trap_frame, &regs);
        ENSURE_ERR(frame, CopyToUser((void *)(*ArchGetFromFrame(frame, 2)), &regs, sizeof(regs)),
                   ERR_BADPTR);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    case MNGTASK_SET_REGS: {
        ENSURE_ERR(frame, target->state != TASK_STATE_ZOMBIE, ERR_DEAD);
        ENSURE_ERR(frame,
                   target->state == TASK_STATE_FAULTED ||
                       (target->suspended && target->state != TASK_STATE_BLOCKED),
                   ERR_BUSY);
        TaskRegs regs;
        ENSURE_ERR(frame,
                   CopyFromUser(&regs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(regs)),
                   ERR_BADPTR);
        ENSURE_ERR(frame, ArchRegsToFrame(target->trap_frame, &regs), ERR_BADARG);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;

    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        break;
    }
}
