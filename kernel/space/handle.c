#include "handle.h"
#include "core/ensure.h"
#include "kernel/ipc/event.h"
#include "kernel/ipc/port.h"
#include "kernel/irq/irq_relay.h"
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

static bool HandleTableHoldsMem(HandleTable *t, const MemObject *mem,
                                const HandleTableEntry *except)
{
    for (Handle i = 0; i < (Handle)HANDLE_MAX_SLOTS; i++) {
        HandleTableEntry *e = HandleTableGet(t, i);
        if (e && e != except && e->type == HANDLE_MEM && e->mem == mem)
            return true;
    }
    return false;
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
    case HANDLE_MEM: {
        MemObject *mem = entry->mem;
        bool device = mem && mem->kind == MEMKIND_DEVICE;
        Irq irq = device ? mem->dev.irq : 0;
        MemObjUnmapAndDrop(sp->as, entry->mapped_va, mem);
        if (device && !HandleTableHoldsMem(&sp->handle_table, mem, entry))
            IrqRelease(sp, irq);
    } break;
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
