#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/pmm.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/err.h>
#include <zuzu/types.h>

void SvcBind(CpuState *frame)
{
    EventType event_type = (EventType)(*ArchGetFromFrame(frame, 0));
    Handle ev_handle = (Handle)(*ArchGetFromFrame(frame, 1));

    /**
     * The only KEvent we support as of now is KEVENT_MEMMGMT which is a memory pressure
     * notification. For future event classes, an event-specific struct will be copied
     * to kernel, and used that way.
     */
    void *data = (void *)*(ArchGetFromFrame(frame, 2));
    (void)data;

    switch (event_type)
    {
    case EVENT_MEMMGMT:
    {
        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, ev_handle);

        if (unlikely(!entry))
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }
        if (unlikely(entry->type != HANDLE_EVENT))
        {
            ArchSetInFrame(frame, 0, ERR_BADTYPE);
            return;
        }

        EventObject *ev = entry->event;

        ENSURE_ERR(frame, (ev), ERR_BADHANDLE);
        ENSURE_ERR(frame, (ev->alive), ERR_DEAD);

        ArchSetInFrame(frame, 0, PmmSubscribe(ev));
        return;
    } break;
    case EVENT_IRQ:
    {
        
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }
}
