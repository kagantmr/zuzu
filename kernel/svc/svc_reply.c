#include "svc.h"
#include <arch/regs.h>

/* ---- reply: delivering into the waiting caller ---- */

static void ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted)
{
    CpuState *target_frame = target->trap_frame;
    ArchSetInFrame(target_frame, 0, ZUZU_OK);
    (*ArchGetFromFrame(target_frame, 1)) = (Register)xlen;
    (*ArchGetFromFrame(target_frame, 3)) = granted;
    if (xlen) LmsgBufCopy(current_task, target, xlen);

    target->ipc_state = IPC_NONE;
    target->blocked_port = NULL;
    target->pending_reply_cap = NULL;
    target->wake_reason = WAKE_IPC;
    target->state = READY;
    SchedAdd(target);
}

void SvcReply(CpuState *frame)
{
    (void)frame;
}
