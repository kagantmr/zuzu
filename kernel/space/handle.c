#include "handle.h"
#include "core/ensure.h"
#include "kernel/ipc/event.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/mem_object.h"
#include "kernel/task/task.h"
#include "space.h"
#include <string.h>

Err HandleCopyInto(HandleTable *dst_table, HandleTableEntry *src, HandlePerms perms, Marker marker,
                   Handle *out)
{
    ENSURE_RET(src->type < HANDLE_TYPE_COUNT && src->type != HANDLE_FREE, ERR_BADTYPE);
    Handle free_handle = HandleTableFindFree(dst_table);
    ENSURE_RET(free_handle >= 0, ERR_NOMEM);
    HandleTableEntry *dst = HandleTableGet(dst_table, free_handle);
    ENSURE_RET(dst, ERR_NOENT);
    HandleEntryClaim(dst_table, dst);

    dst->perms = perms;
    dst->type = src->type;
    dst->marker = marker;
    dst->mapped_va = 0;
    switch (src->type) {
    case HANDLE_PORT:
        dst->port = src->port;
        src->port->ref_count++;
        break;
    case HANDLE_EVENT:
        dst->event = src->event;
        src->event->ref_count++;
        break;
    case HANDLE_MEM:
        dst->mem = src->mem;
        src->mem->ref_count++;
        break;
    case HANDLE_TASK:
        dst->task = src->task;
        TaskRef(src->task);
        break;
    case HANDLE_SPACE:
        dst->space = src->space;
        SpaceRef(src->space);
        break;
    default:
        break;
    }

    *out = (Handle)HANDLE_PACK(free_handle, dst->generation);
    return ZUZU_OK;
}

void HandleRelease(SpaceObject *sp, HandleTableEntry *entry)
{
    switch (entry->type) {
    case HANDLE_PORT:
        PortUnref(entry->port);
        break;
    case HANDLE_EVENT:
        EventUnref(entry->event);
        break;
    case HANDLE_MEM:
        MemObjUnmapAndDrop(sp->as, entry->mapped_va, entry->mem);
        break;
    case HANDLE_TASK:
        TaskUnref(entry->task);
        break;
    case HANDLE_SPACE:
        SpaceUnref(entry->space);
        break;
    case HANDLE_FREE:
        return;
    default:
        break;
    }
    HandleEntryFree(&sp->handle_table, entry);
}
