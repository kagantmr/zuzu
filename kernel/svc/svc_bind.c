#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/irq/irq_relay.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/err.h>
#include <types.h>

static HandleTableEntry *BindLookupTarget(CpuState *frame, Handle h, HandleType want_type, uint32_t bit)
{
    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, h);
    ENSURE(entry, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return NULL);
    ENSURE((entry->type == want_type), ArchSetInFrame(frame, 0, ERR_BADTYPE); return NULL);
    ENSURE((entry->perms & PERM_WAIT), ArchSetInFrame(frame, 0, ERR_NOPERM); return NULL);
    ENSURE((bit < 31U), ArchSetInFrame(frame, 0, ERR_BADARG); return NULL);
    return entry;
}

static void BindEventTo(EventObject **slot, uint32_t *bit_slot, EventObject *ev, uint32_t bit)
{
    if (*slot) {
        (*slot)->bind_count--;
        EventDropReference(*slot);
    }
    *slot = ev;
    ev->ref_count++;
    ev->bind_count++;
    ev->bound_mask |= (1U << bit);
    *bit_slot = bit;
}

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
        Handle h = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *target = BindLookupTarget(frame, h, HANDLE_PORT, bit);
        if (!target) return;

        PortObject *port = target->port;
        ENSURE_ERR(frame, port, ERR_BADHANDLE);
        ENSURE_ERR(frame, port->alive, ERR_DEAD);

        BindEventTo(&port->bound_ev, &port->bind_bit, ev, bit);
        PortMaybeSignalBind(port);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    case EVENT_TASK:
    {
        Handle h = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *target = BindLookupTarget(frame, h, HANDLE_TASK, bit);
        if (!target) return;

        TaskObject *task = target->task;
        ENSURE_ERR(frame, task, ERR_BADHANDLE);

        BindEventTo(&task->bound_ev, &task->bind_bit, ev, bit);
        TaskMaybeSignalBind(task);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    case EVENT_SPACE:
    {
        Handle h = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *target = BindLookupTarget(frame, h, HANDLE_SPACE, bit);
        if (!target) return;

        SpaceObject *space = target->space;
        ENSURE_ERR(frame, space, ERR_BADHANDLE);

        BindEventTo(&space->bound_ev, &space->bind_bit, ev, bit);
        SpaceMaybeSignalBind(space);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}