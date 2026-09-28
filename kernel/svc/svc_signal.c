#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/err.h>
#include <zuzu/types.h>

void SvcSignal(CpuState *frame)
{
    Handle ev_handle = (Handle)(*ArchGetFromFrame(frame, 0));
    EventWord bits = (EventWord)(*ArchGetFromFrame(frame, 1));
    EventWord flags = (EventWord)(*ArchGetFromFrame(frame, 2));

    
    ENSURE_ERR(frame, !(bits & (1U << 31)), ERR_BADARG);

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, ev_handle);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, entry->perms & PERM_SEND, ERR_NOPERM);
    ENSURE_ERR(frame, (entry->type == HANDLE_EVENT), ERR_BADTYPE);

    EventObject *ev = entry->event;

    ENSURE_ERR(frame, ev, ERR_DEAD);
    ENSURE_ERR(frame, (ev->bind_count == 0), ERR_NOPERM);
    ENSURE_ERR(frame, (ev->alive), ERR_DEAD);

    ENSURE_ERR(frame, !(flags & ~SIGNAL_BROADCAST), ERR_BADARG);
    EventSignal(ev, bits, (flags & SIGNAL_BROADCAST) != 0);

    ArchSetInFrame(frame, 0, ZUZU_OK);
}
