#include "sys_event.h"

#include "kernel/ipc/handle.h"
#include "kernel/ipc/ntfn.h"
#include "kernel/mm/pmm.h"
#include "kernel/proc/thread.h"
#include "kernel/proc/process.h"

#include <assert.h>
#include <zuzu/types.h>
#include <zuzu/err.h>

extern TaskObject *current_task;

void SysKEventBind(CpuState *frame)
{
    if (!frame)
        return;

    EventType event_type = *(ArchGetFromFrame(frame, 0));
    Handle h = (Handle)(*ArchGetFromFrame(frame, 1));

    /**
     * The only KEvent we support as of now is KEVENT_MEMMGMT which is a memory pressure
     * notification. For future event classes, an event-specific struct will be copied
     * to kernel, and used that way.
     */
    void *data = (void *)*(ArchGetFromFrame(frame, 2));
    (void)data;

    switch (event_type) {
    case KEVENT_MEMMGMT: {
        HandleTableEntry *entry = HandleTableGet(&current_task->owner_process->handle_table, (uint32_t)h);

        if (unlikely(!entry)) {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }
        if (unlikely(entry->type != HANDLE_NTFN)) {
            ArchSetInFrame(frame, 0, ERR_BADTYPE);
            return;
        }

        EventObject *ntfn = entry->ntfn;

        assert(ntfn);

        if (!ntfn->alive) {
            ArchSetInFrame(frame, 0, ERR_DEAD);
            return;
        }

        ArchSetInFrame(frame, 0, PmmSubscribe(ntfn));
        return;
    };
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }
}