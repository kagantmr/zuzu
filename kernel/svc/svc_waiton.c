#include "kernel/ipc/msg.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <core/ensure.h>

void SvcWaitOn(CpuState *frame)
{
    Handle h = (Handle)(*ArchGetFromFrame(frame, 0));
    Duration timeout = (Duration)(*ArchGetFromFrame(frame, 1));

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, h);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, (entry->perms & PERM_WAIT), ERR_NOPERM);

    switch (entry->type) {
    case HANDLE_PORT: {
        PortReceive(entry->port, timeout, frame);
    } break;
    case HANDLE_EVENT: {
        EventWait(entry->event, timeout, frame);
    } break;
    case HANDLE_SPACE: {
        SpaceWaitHollow(entry->space, timeout, frame);
    } break;
    case HANDLE_TASK: {
        TaskWaitExit(entry->task, timeout, frame);
    } break;

    default:
        ENSURE_ERR(frame, 0, ERR_BADTYPE);
    }
}
