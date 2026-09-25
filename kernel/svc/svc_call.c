#include "svc.h"
#include <arch/regs.h>
#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "kernel/space/space.h"
#include "kernel/sched/sched.h"


void __hot SvcCall(CpuState *frame)
{
    Handle handle = (Handle)(*ArchGetFromFrame(frame, 0));
    uint32_t xlen = (uint32_t)(*ArchGetFromFrame(frame, 1));
    Handle grant_handle = (Handle)(*ArchGetFromFrame(frame, 2));
    ENSURE_ERR(frame, (xlen <= MSG_BUF_SIZE), ERR_OVERFLOW);

    HandleTableEntry *entry = ValidateCallPort(CURRENT_SPACE, handle, frame);
    if (!entry) return;
    PortObject *port = entry->port;

    Err grant_err = ValidateGrantHandle(CURRENT_SPACE, grant_handle);
    ENSURE_ERR(frame, (grant_err == ZUZU_OK), grant_err);

    EphemeralReplyObject *rc = &current_task->reply_cap_storage;
    rc->caller_task = current_task;
    rc->caller_tid = current_task->tid;

    current_task->port_marker = entry->marker;

    if (!list_empty(&port->receiver_queue)) {
        if (!CallHandoffToReceiver(current_task, port, rc, xlen, grant_handle, frame)) {
            return;
        }
    } else {
        CallBlockAsSender(current_task, port, rc, xlen, grant_handle);
    }
}
