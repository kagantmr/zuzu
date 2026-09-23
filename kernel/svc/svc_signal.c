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

    ENSURE_ERR(frame, !(bits & (1U << 31)), ERR_BADARG);

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, ev_handle);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, (entry->type == HANDLE_EVENT), ERR_BADTYPE);

    EventObject *ev = entry->event;

    ENSURE_ERR(frame, ev, ERR_DEAD);
    ENSURE_ERR(frame, (ev->alive), ERR_DEAD);

    EventSignal(ev, bits);

    ArchSetInFrame(frame, 0, ZUZU_OK);
}
