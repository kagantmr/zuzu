#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <util/tls.h>

void SvcReply(CpuState *frame)
{
    size_t xlen = (size_t)(*ArchGetFromFrame(frame, 0));
    Handle grant_handle = (Handle)(*ArchGetFromFrame(frame, 1));
    Handle recv_handle = (Handle)(*ArchGetFromFrame(frame, 2));
    Duration timeout = (Duration)(*ArchGetFromFrame(frame, 3));

    HandleTableEntry *recv_entry = NULL;
    if (recv_handle != -1) {
        recv_entry = HandleTableLookup(&CURRENT_SPACE->handle_table, recv_handle);
        ENSURE_ERR(frame, recv_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, recv_entry->type == HANDLE_PORT, ERR_BADTYPE);
        ENSURE_ERR(frame, (recv_entry->perms & PERM_WAIT), ERR_NOPERM);
    }

    ENSURE_ERR(frame, (xlen <= MSG_BUF_SIZE), ERR_OVERFLOW);

    ReplyObject *rc = current_task->reply_cap;

    if (rc) {
        TaskObject *target = rc->caller_task;
        if (!target || target->tid != rc->caller_tid || target->state == TASK_STATE_ZOMBIE ||
            target->ipc_state != IPC_WAITING)
        {
            current_task->reply_cap = NULL;
            if (recv_handle == -1) {
                ArchSetInFrame(frame, 0, ERR_DEAD);
                return;
            }
        } else {
            Handle granted;
            Err grant_err = GrantHandleAcross(CURRENT_SPACE, target->owner, grant_handle, &granted);
            current_task->reply_cap = NULL;
            if (grant_err != ZUZU_OK) {
                ReplyFailCaller(target, grant_err);
                ArchSetInFrame(frame, 0, grant_err);
                return;
            }
            ReplyDeliverToCaller(target, xlen, granted);
        }
    } else {
        ENSURE_ERR(frame, -1 != recv_handle, ERR_BADHANDLE);
    }

    if (-1 == recv_handle) {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        return;
    }

    PortReceive(recv_entry->port, timeout, frame);
}
