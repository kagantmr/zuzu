/**
 * @file svc_create.c
 * @brief Implementation of the Create() svc.
 */

#include "core/ensure.h"
#include "kernel/clock.h"
#include "kernel/ipc/port.h"
#include "kernel/space/space.h"
#include "svc.h"
#include "kernel/checkpoint/checkpoint.h"
#include <arch/cache.h>
#include <arch/regs.h>
#include <types.h>
#include <zuzu/err.h>

/**
 * @brief Drops a half-built memory object left in a dead task's checkpoint state.
 *
 * @param[in] obj The MemObject.
 */
static void DropMem(void *obj) { MemObjUnref(obj); }

void SvcCreate(CpuState *frame)
{
    ZuzuObjectCode type = (ZuzuObjectCode)(*ArchGetFromFrame(frame, 0));
    switch (type) {
    case OBJECT_TASK: {
        Handle space_handle = (Handle)(*ArchGetFromFrame(frame, 1));
        SpaceObject *target_space;

        if (-1 == space_handle) {
            target_space = CURRENT_SPACE;
        } else {
            HandleTableEntry *space_entry =
                HandleTableLookup(&CURRENT_SPACE->handle_table, space_handle);

            ENSURE_ERR(frame, (NULL != space_entry), ERR_BADHANDLE);
            ENSURE_ERR(frame, (HANDLE_SPACE == space_entry->type), ERR_BADTYPE);
            ENSURE_ERR(frame, space_entry->perms & PERM_CNTL, ERR_NOPERM);
            ENSURE_ERR(frame, (NULL != space_entry->space), ERR_BADHANDLE);
            target_space = space_entry->space;
            ENSURE_ERR(frame, !target_space->torn_down, ERR_DEAD);
        }

        TaskObject *task = TaskCreate(target_space);

        ENSURE_ERR(frame, (NULL != task), ERR_BUSY);
        task->max_prio = (current_task->max_prio < target_space->max_prio) ? current_task->max_prio
                                                                           : target_space->max_prio;

        task->base_prio =
            (current_task->base_prio < task->max_prio) ? current_task->base_prio : task->max_prio;
        task->priority = task->base_prio;
        TaskSyncTcbPrio(task);
        task->max_time_slice_us = current_task->max_time_slice_us;
        if (task->time_slice_us > task->max_time_slice_us) {
            task->time_slice_us = task->max_time_slice_us;
            task->time_slice_counts = ClockUsToCounts(task->time_slice_us);
        }

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, TaskDestroy(task); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableClaim(&CURRENT_SPACE->handle_table, new_handle);
        entry->type = HANDLE_TASK;
        entry->task = task;
        entry->perms = PERM_ALL;
        TaskRef(task);

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_PORT: {
        PortObject *new_port = PortCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_port), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, PortUnref(new_port); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableClaim(&CURRENT_SPACE->handle_table, new_handle);
        entry->type = HANDLE_PORT;
        entry->port = new_port;
        entry->perms = PERM_ALL;

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_EVENT: {
        EventObject *new_event = EventCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_event), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, EventDestroy(new_event); ArchSetInFrame(frame, 0, ERR_NOMEM);
               return);

        HandleTableEntry *entry = HandleTableClaim(&CURRENT_SPACE->handle_table, new_handle);
        entry->type = HANDLE_EVENT;
        entry->event = new_event;
        entry->perms = PERM_ALL;

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_SPACE: {
        VirtAddr name_ptr = (VirtAddr)(*ArchGetFromFrame(frame, 1));
        size_t name_len = (size_t)(*ArchGetFromFrame(frame, 2));

        char kname[32];
        if (name_len >= sizeof(kname))
            name_len = sizeof(kname) - 1;

        ENSURE_ERR(frame, (name_len == 0 || CopyFromUser(kname, (const void *)name_ptr, name_len)),
                   ERR_BADPTR);
        kname[name_len] = '\0';

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE_ERR(frame, (-1 != new_handle), ERR_NOMEM);

        SpaceObject *space = SpaceCreate(kname, CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != space), ERR_NOMEM);
        space->max_prio = current_task->max_prio;

        HandleTableEntry *entry = HandleTableClaim(&CURRENT_SPACE->handle_table, new_handle);
        entry->type = HANDLE_SPACE;
        entry->space = space;
        entry->perms = PERM_ALL;
        SpaceRef(space);

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_MEMORY: {
        CreateMemoryFlags flags = (CreateMemoryFlags)(*ArchGetFromFrame(frame, 2));
        size_t page_count = (size_t)(*ArchGetFromFrame(frame, 1));
        CheckpointState *cp = &current_task->checkpoint;
        MemObject *mem = NULL;
        size_t pos = 0;

        if (cp->kind == CHECKPOINT_CREATE_MEM) {
            mem = cp->obj;
            pos = cp->pos;
            if (mem->shm.page_count != page_count || mem->shm.flags != flags) {
                MemObjUnref(mem);
                mem = NULL;
                pos = 0;
            }
            *cp = (CheckpointState){0};
        }

        if (!mem) {
            ENSURE_ERR(frame, !(flags & (unsigned)~MEM_FLAGS_ALL), ERR_BADARG);
            ENSURE_ERR(frame, !(flags & MEM_UNCACHED) || (flags & MEM_CONTIG), ERR_BADARG);
            ENSURE_ERR(frame, (page_count > 0), ERR_BADARG);
            ENSURE_ERR(frame, (flags & MEM_CONTIG) || page_count <= MEM_MAX_PAGES, ERR_OVERFLOW);
            ENSURE_ERR(frame, page_count <= PmmGetStats().free_frames, ERR_NOMEM);
            ENSURE_ERR(frame, page_count <= SIZE_MAX / sizeof(PhysAddr), ERR_NOMEM);

            PhysAddr *page_addrs = KZAlloc(page_count * sizeof(PhysAddr));
            ENSURE_ERR(frame, (NULL != page_addrs), ERR_NOMEM);

            mem = MemObjCreateShm(page_addrs, page_count, flags);
            ENSURE(NULL != mem, KFree(page_addrs); ArchSetInFrame(frame, 0, ERR_NOMEM); return);
            mem->owner_spid = CURRENT_SPACE->spid;

            if (flags & MEM_CONTIG) {
                PhysAddr base = PmmAllocFramesContig(page_count);
                ENSURE(PA_NULL != base, MemObjUnref(mem); ArchSetInFrame(frame, 0, ERR_NOMEM);
                       return);
                for (size_t i = 0; i < page_count; i++)
                    page_addrs[i] = base + (i * PAGE_SIZE);
            }
        }

        if (flags & MEM_CONTIG) {
            VirtAddr base_va = PA_TO_VA(mem->shm.page_addrs[0]);
            while (pos < page_count) {
                memset((void *)(base_va + pos * PAGE_SIZE), 0, PAGE_SIZE);
                if (flags & MEM_UNCACHED)
                    ArchCacheCleanInvalidateDcacheRange(base_va + pos * PAGE_SIZE, PAGE_SIZE);
                pos++;
                if (pos < page_count && CheckpointDue()) {
                    *cp = (CheckpointState){
                        .kind = CHECKPOINT_CREATE_MEM, .obj = mem, .pos = pos, .drop = DropMem};
                    ArchRewindSvc(frame);
                    return;
                }
            }
        }

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, MemObjUnref(mem); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableClaim(&CURRENT_SPACE->handle_table, new_handle);
        entry->type = HANDLE_MEM;
        entry->mem = mem;
        entry->perms = PERM_ALL;
        entry->mapped_va = 0;

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    default:
        ENSURE_ERR(frame, 0, ERR_BADARG);
    }
}
