#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/tls.h>

void SvcReply(CpuState *frame)
{
    size_t xlen = (size_t)(*ArchGetFromFrame(frame, 0));
    Handle grant_handle = (Handle)(*ArchGetFromFrame(frame, 1));
    ENSURE_ERR(frame, (xlen <= MSG_BUF_SIZE), ERR_OVERFLOW);

    EphemeralReplyObject *rc = current_task->reply_cap;
    ENSURE_ERR(frame, rc, ERR_BADHANDLE);
    // clear reply cap
    current_task->reply_cap = NULL;

    TaskObject *target = rc->caller_task;
    if (!target || target->tid != rc->caller_tid || target->state == ZOMBIE ||
        target->ipc_state != IPC_WAITING)
    {
        ArchSetInFrame(frame, 0, ERR_DEAD);
        return;
    }

    Handle granted;
    Err grant_err = GrantHandleAcross(CURRENT_SPACE, target->owner, grant_handle, &granted);
    ENSURE_ERR(frame, (grant_err == ZUZU_OK), grant_err);

    ReplyDeliverToCaller(target, xlen, granted);

    ArchSetInFrame(frame, 0, ZUZU_OK);
}
