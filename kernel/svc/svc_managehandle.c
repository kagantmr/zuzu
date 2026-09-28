#include "core/ensure.h"
#include "kernel/space/handle.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>

void SvcManageHandle(CpuState *frame)
{
    Handle h = (Handle)(*ArchGetFromFrame(frame, 0));
    ManageHandleVerb verb = (ManageHandleVerb)(*ArchGetFromFrame(frame, 1));

    HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, h);

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
    case MNGHNDL_RESTRICT: {
        /* r2 = perms mask */
        HandlePerms mask = (HandlePerms)(*ArchGetFromFrame(frame, 2));
        entry->perms &= mask;
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    
    case MNGHNDL_CLOSE: {
        if (HANDLE_SPACE == entry->type) {
            SpaceObject *target = entry->space;
            ENSURE_ERR(frame, target, ERR_BADHANDLE);
            SpaceObject *walk = CURRENT_SPACE;
            bool is_self_or_ancestor = false;
            while (walk) {
                if (walk == target) {
                    is_self_or_ancestor = true;
                    break;
                }
                walk = (walk->parent_spid == -1) ? NULL : SpaceFindBySpid(walk->parent_spid);
            }
            ENSURE_ERR(frame, !is_self_or_ancestor, ERR_BADARG);
        }
        HandleRelease(CURRENT_SPACE, entry);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    case MNGHNDL_DESTROY: {
        if (HANDLE_SPACE == entry->type) {
            /* same self/ancestor walk CLOSE already does */
            SpaceObject *target = entry->space;
            ENSURE_ERR(frame, target, ERR_BADHANDLE);
            SpaceObject *walk = CURRENT_SPACE;
            bool is_self_or_ancestor = false;
            while (walk) {
                if (walk == target) { is_self_or_ancestor = true; break; }
                walk = (walk->parent_spid == -1) ? NULL : SpaceFindBySpid(walk->parent_spid);
            }
            ENSURE_ERR(frame, !is_self_or_ancestor, ERR_BADARG);
            SpaceDestroy(target);
        }
        HandleRelease(CURRENT_SPACE, entry);
        ArchSetInFrame(frame, 0, ZUZU_OK);
    } break;
    case MNGHNDL_QUERY: {
        /* r2 = what */
        QueryWhat what = (QueryWhat)(*ArchGetFromFrame(frame, 2));
        Register value;
        switch (what) {
            case QUERY_TYPE:   value = (Register)entry->type;  break;
            case QUERY_PERMS:  value = (Register)entry->perms; break;
            case QUERY_MARKER: value = (Register)entry->marker; break;
            case QUERY_STATUS: {
                if (HANDLE_TASK == entry->type) {
                    ENSURE_ERR(frame, entry->task, ERR_BADHANDLE);
                    value = (Register)entry->task->exit_status;
                } else if (HANDLE_SPACE == entry->type) {
                    ENSURE_ERR(frame, entry->space, ERR_BADHANDLE);
                    value = (Register)entry->space->last_exit_status;
                } else {
                    ArchSetInFrame(frame, 0, ERR_BADTYPE);
                    return;
                }
            } break;
            default:
                ArchSetInFrame(frame, 0, ERR_BADARG);
                return;
        }
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, value);
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        break;
    }
}
