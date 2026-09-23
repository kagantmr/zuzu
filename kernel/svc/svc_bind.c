#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/pmm.h"
#include "kernel/irq/irq_relay.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/err.h>
#include <zuzu/types.h>

void SvcBind(CpuState *frame)
{
    EventType event_type = (EventType)(*ArchGetFromFrame(frame, 0));
    Handle ev_handle = (Handle)(*ArchGetFromFrame(frame, 1));

    HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, ev_handle);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, (entry->type == HANDLE_EVENT), ERR_BADTYPE);

    EventObject *ev = entry->event;

    ENSURE_ERR(frame, (ev), ERR_BADHANDLE);
    ENSURE_ERR(frame, (ev->alive), ERR_DEAD);
    
    switch (event_type)
    {
    case EVENT_MEMMGMT:
    {
        ArchSetInFrame(frame, 0, PmmSubscribe(ev));
    } break;
    case EVENT_IRQ:
    {
        Handle dev_handle = (Handle)(*ArchGetFromFrame(frame, 2));
        HandleTableEntry *dev_entry = HandleTableGet(&CURRENT_SPACE->handle_table, dev_handle);
    
        ENSURE_ERR(frame, dev_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, (dev_entry->type == HANDLE_MEM), ERR_BADTYPE);
    
        DeviceObject *dev = dev_entry->dev;
    
        ENSURE_ERR(frame, (dev), ERR_BADHANDLE);

        
        ArchSetInFrame(frame, 0, IrqBindToEvent(CURRENT_SPACE, dev->irq, ev));
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}
