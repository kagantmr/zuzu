/**
 * @file svc_bind.c
 * @brief Implementation of the Bind() svc.
 */

#include "core/ensure.h"
#include "kernel/ipc/observer.h"
#include "kernel/ipc/port.h"
#include "kernel/irq/irq_relay.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <types.h>
#include <zuzu/err.h>

/**
 * @brief Represents a Bind() target.
 */
typedef struct {
    ObserverSet *set; /**< Set of observers to bind into. */
    bool ready;       /**< If the target is ready. */
} BindTarget;

/**
 * @brief Finds and validates the target of the Bind().
 * 
 * @param[in] type The type of event to bind.
 * @param[in] h The handle to bind to.
 * @param[in] bit The bit to bind to.
 * @param[out] out The validated binding target.
 * 
 * @return Err
 * @retval ERR_BADHANDLE This handle is invalid.
 * @retval ERR_BADTYPE   This handle cannot be bound to.
 * @retval ERR_NOPERM    You do not have the permissions to bind to this.
 * @retval ERR_BADARG    This bit is reserved.
 * @retval ERR_DEAD      The port you are trying to bind to has died.
 */
static Err BindLookupTarget(EventType type, Handle h, uint32_t bit, BindTarget *out)
{
    HandleType want = (type == EVENT_PORT)   ? HANDLE_PORT
                      : (type == EVENT_TASK) ? HANDLE_TASK
                                             : HANDLE_SPACE;
    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, h);
    if (!entry)
        return ERR_BADHANDLE;
    if (entry->type != want)
        return ERR_BADTYPE;
    if (!(entry->perms & PERM_WAIT))
        return ERR_NOPERM;
    if (bit >= 31U)
        return ERR_BADARG;

    switch (type) {
    case EVENT_PORT: {
        PortObject *port = entry->port;
        if (!port)
            return ERR_BADHANDLE;
        if (!port->alive)
            return ERR_DEAD;
        *out = (BindTarget){&port->observers, PortHasPending(port)};
    } break;
    case EVENT_TASK: {
        TaskObject *task = entry->task;
        if (!task)
            return ERR_BADHANDLE;
        *out = (BindTarget){task->released ? NULL : &task->observers, TaskIsDead(task)};
    } break;
    default: {
        SpaceObject *space = entry->space;
        if (!space)
            return ERR_BADHANDLE;
        *out = (BindTarget){&space->observers, SpaceIsHollow(space)};
    } break;
    }
    return ZUZU_OK;
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

    switch (event_type) {
    case EVENT_MEMMGMT: {
        ArchSetInFrame(frame, 0, PmmSubscribe(ev));
    } break;
    case EVENT_IRQ: {
        Handle dev_handle = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        HandleTableEntry *dev_entry = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);

        ENSURE_ERR(frame, dev_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, dev_entry->type == HANDLE_MEM, ERR_BADTYPE);

        MemObject *dev_mem_obj = dev_entry->mem;

        ENSURE_ERR(frame, (dev_mem_obj), ERR_BADHANDLE);
        ENSURE_ERR(frame, dev_mem_obj->kind == MEMKIND_DEVICE, ERR_BADTYPE);
        ENSURE_ERR(frame, dev_entry->perms & PERM_MAP, ERR_NOPERM);

        ENSURE_ERR(frame, IrqIsValid(dev_mem_obj->dev.irq), ERR_BADARG);
        ENSURE_ERR(frame, (bit < 31U), ERR_BADARG);

        ArchSetInFrame(frame, 0, IrqBindToEvent(CURRENT_SPACE, dev_mem_obj->dev.irq, ev, bit));
    } break;
    case EVENT_PORT:
    case EVENT_TASK:
    case EVENT_SPACE: {
        Handle h = (Handle)(*ArchGetFromFrame(frame, 2));
        uint32_t bit = (uint32_t)(*ArchGetFromFrame(frame, 3));
        BindTarget target;
        Err rc = BindLookupTarget(event_type, h, bit, &target);
        if (rc == ZUZU_OK && target.set)
            rc = ObserverAdd(target.set, ev, bit);
        if (rc == ZUZU_OK && target.ready)
            EventSignal(ev, (1U << bit), false);
        ArchSetInFrame(frame, 0, rc);
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}
