#include "sys_event.h"

#include "kernel/ipc/handle.h"
#include "kernel/ipc/ntfn.h"
#include "kernel/mm/pmm.h"
#include "kernel/proc/thread.h"
#include "kernel/proc/process.h"

#include <assert.h>
#include <zuzu/types.h>
#include <zuzu/err.h>

extern Thread *current_thread;

void SysKEventBind(CpuState *frame)
{
    if (!frame)
        return;

    KEventType event_type = *(arch_reg(frame, 0));
    Handle h = *(arch_reg(frame, 1));

    /**
     * The only KEvent we support as of now is KEVENT_MEMMGMT which is a memory pressure
     * notification. For future event classes, an event-specific struct will be copied
     * to kernel, and used that way.
     */
    void *data = (void *)*(arch_reg(frame, 2));
    (void)data;

    switch (event_type) {
    case KEVENT_MEMMGMT: {
        HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, h);

        if (unlikely(!entry)) {
            *(arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }
        if (unlikely(entry->type != HANDLE_NTFN)) {
            *(arch_reg(frame, 0)) = ERR_BADTYPE;
            return;
        }

        NtfnObj *ntfn = entry->ntfn;

        assert(ntfn);

        if (!ntfn->alive) {
            *(arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        *(arch_reg(frame, 0)) = PmmSubscribe(ntfn);
        return;
    };
    default:
        *(arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
}