#include "core/ensure.h"
#include "kernel/irq/irq_relay.h"
#include "kernel/space/handle.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>

void SvcManageHandle(CpuState *frame)
{
    Handle h = (Handle)(*ArchGetFromFrame(frame, 0));
    ManageHandleVerb verb = (ManageHandleVerb)(*ArchGetFromFrame(frame, 1));

    HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, h);

    ENSURE_ERR(frame, entry, ERR_BADHANDLE);

    switch (verb)
    {
    case MNGHNDL_DUPLICATE:
    {
        Marker handle_marker = (Marker)(*ArchGetFromFrame(frame, 3));
        HandlePerms perms = (HandlePerms)(*ArchGetFromFrame(frame, 2));
        if (!(HANDLE_PORT == entry->type && entry->port->owner_spid == CURRENT_SPACE->spid))
            handle_marker = entry->marker;
        Handle new_handle;
        Err rc = HandleCopyInto(&CURRENT_SPACE->handle_table, entry, entry->perms & perms,
                                handle_marker, &new_handle);
        ENSURE_ERR(frame, ZUZU_OK == rc, rc);
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, new_handle);
    }
    break;
    case MNGHNDL_RESTRICT:
    {
        /* r2 = perms mask */
        HandlePerms mask = (HandlePerms)(*ArchGetFromFrame(frame, 2));
        entry->perms &= mask;
        ArchSetInFrame(frame, 0, ZUZU_OK);
    }
    break;

    case MNGHNDL_CLOSE:
    {
        if (HANDLE_SPACE == entry->type)
        {
            ENSURE_ERR(frame, entry->space, ERR_BADHANDLE);
            ENSURE_ERR(frame, !SpaceIsSelfOrAncestor(CURRENT_SPACE, entry->space), ERR_BADARG);
        }
        HandleRelease(CURRENT_SPACE, entry);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    }
    break;
    case MNGHNDL_DESTROY:
    {
        if (HANDLE_SPACE == entry->type)
        {
            ENSURE_ERR(frame, entry->space, ERR_BADHANDLE);
            ENSURE_ERR(frame, !SpaceIsSelfOrAncestor(CURRENT_SPACE, entry->space), ERR_BADARG);
            SpaceDestroy(entry->space);
        }
        HandleRelease(CURRENT_SPACE, entry);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    }
    break;
    case MNGHNDL_QUERY:
    {
        /* r2 = what */
        QueryWhat what = (QueryWhat)(*ArchGetFromFrame(frame, 2));
        Register value;
        switch (what)
        {
        case QUERY_TYPE:
            value = (Register)entry->type;
            break;
        case QUERY_PERMS:
            value = (Register)entry->perms;
            break;
        case QUERY_MARKER:
            value = (Register)entry->marker;
            break;
        case QUERY_STATUS:

            if (HANDLE_TASK == entry->type)
            {
                ENSURE_ERR(frame, entry->task, ERR_BADHANDLE);
                value = (Register)entry->task->exit_status;
            }
            else if (HANDLE_SPACE == entry->type)
            {
                ENSURE_ERR(frame, entry->space, ERR_BADHANDLE);
                value = (Register)entry->space->last_exit_status;
            }
            else if (HANDLE_PORT == entry->type)
            {
                ENSURE_ERR(frame, entry->port, ERR_BADHANDLE);
                value = (Register)(entry->port->alive ? ZUZU_OK : ERR_DEAD);
            }
            else
            {
                ArchSetInFrame(frame, 0, ERR_BADTYPE);
                return;
            }
            break;
        case QUERY_SIZE:
            ENSURE_ERR(frame, HANDLE_MEM == entry->type, ERR_BADTYPE);
            ENSURE_ERR(frame, entry->mem, ERR_BADHANDLE);
            value = (entry->mem->kind == MEMTYPE_DEVICE)
                        ? (Register)entry->mem->dev.size
                        : (Register)(entry->mem->shm.page_count * PAGE_SIZE);

            break;
        default:
            ArchSetInFrame(frame, 0, ERR_BADARG);
            return;
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, value);
    }
    break;
    case MNGHNDL_GRANT:
    {
        Handle target_space_handle = (Handle)(*ArchGetFromFrame(frame, 2));
        HandlePerms perms = (HandlePerms)(*ArchGetFromFrame(frame, 3));

        HandleTableEntry *space_entry =
            HandleTableLookup(&CURRENT_SPACE->handle_table, target_space_handle);
        ENSURE_ERR(frame, space_entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, HANDLE_SPACE == space_entry->type, ERR_BADTYPE);
        ENSURE_ERR(frame, space_entry->perms & PERM_CNTL, ERR_NOPERM);
        SpaceObject *target_space = space_entry->space;
        ENSURE_ERR(frame, target_space && !target_space->torn_down, ERR_DEAD);

        Handle new_handle;
        Err rc = HandleCopyInto(&target_space->handle_table, entry, entry->perms & perms,
                                entry->marker, &new_handle);
        ENSURE_ERR(frame, ZUZU_OK == rc, rc);
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, new_handle);
    }
    break;
    case MNGHNDL_IRQ_REARM:
    {
        ENSURE_ERR(frame, HANDLE_MEM == entry->type, ERR_BADTYPE);
        MemObject *dev = entry->mem;
        ENSURE_ERR(frame, dev && dev->kind == MEMTYPE_DEVICE, ERR_BADTYPE);
        ENSURE_ERR(frame, IrqIsValid(dev->dev.irq), ERR_BADARG);
        ArchSetInFrame(frame, 0, IrqRelayRearm(CURRENT_SPACE, dev->dev.irq));
    }
    break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        break;
    }
}
