#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/pmm/pmm.h"
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

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, ev_handle);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);
    ENSURE_ERR(frame, (entry->type == HANDLE_EVENT), ERR_BADTYPE);
    ENSURE_ERR(frame, entry->perms & PERM_CNTL, ERR_NOPERM);
    
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
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *dev_entry = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);

        ENSURE_ERR(frame, dev_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, (dev_entry->type == HANDLE_MEM && dev_entry->mem->kind == MEMTYPE_DEVICE), ERR_BADTYPE);

        MemObject *dev_mem_obj = dev_entry->mem;

        ENSURE_ERR(frame, (dev_mem_obj), ERR_BADHANDLE);

        ENSURE_ERR(frame, IrqIsValid(dev_mem_obj->dev.irq), ERR_BADARG);
        ENSURE_ERR(frame, (bit < 31U), ERR_BADARG);

        ArchSetInFrame(frame, 0, IrqBindToEvent(CURRENT_SPACE, dev_mem_obj->dev.irq, ev, bit));
    } break;
    case EVENT_PORT:
    {
        Handle dev_handle = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *port_entry = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);

        ENSURE_ERR(frame, port_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, (port_entry->type == HANDLE_PORT), ERR_BADTYPE);

        PortObject *port_obj = port_entry->port;

        ENSURE_ERR(frame, (port_obj), ERR_BADHANDLE);
        ENSURE_ERR(frame, (bit < 31U), ERR_BADARG);

        if (port_obj->bound_ev) {
            port_obj->bound_ev->bind_count--; 
            EventDropReference(port_obj->bound_ev);
        }

        port_obj->bound_ev = ev; 
        ev->ref_count++;
        ev->bind_count++;
        
        PortMaybeSignalBind(port_obj);
        
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}
