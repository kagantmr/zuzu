#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/cache.h>
#include <arch/regs.h>
#include <types.h>
#include <zuzu/err.h>

void SvcCreate(CpuState *frame)
{
    // Dispatch based on type
    ZuzuObjectCode type = (ZuzuObjectCode)(*ArchGetFromFrame(frame, 0));
    switch (type) {
    case OBJECT_TASK: {
        // TASK: r0=type, r1=space_handle (a kitten Space you hold a handle
        // to, or -1 to spawn a sibling Task in your own Space)
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

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, TaskDestroy(task); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_TASK;
        entry->task = task;
        entry->perms = PERM_ALL;
        TaskRef(task);

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_PORT: {
        // PORT: r0=type
        PortObject *new_port = PortCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_port), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, PortUnref(new_port); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_PORT;
        entry->port = new_port;
        entry->perms = PERM_ALL;

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_EVENT: {
        // EVENT: r0=type
        EventObject *new_event = EventCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_event), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, EventDestroy(new_event); ArchSetInFrame(frame, 0, ERR_NOMEM);
               return);

        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_EVENT;
        entry->event = new_event;
        entry->perms = PERM_ALL;

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_SPACE: {
        // SPACE: r0=type, r1=name_ptr, r2=name_len
        VirtAddr name_ptr = (VirtAddr)(*ArchGetFromFrame(frame, 1));
        size_t name_len = (size_t)(*ArchGetFromFrame(frame, 2));

        char kname[32]; // matches SpaceObject.name[32]
        if (name_len >= sizeof(kname))
            name_len = sizeof(kname) - 1;

        ENSURE_ERR(frame, (name_len == 0 || CopyFromUser(kname, (const void *)name_ptr, name_len)),
                   ERR_BADPTR);
        kname[name_len] = '\0';

        SpaceObject *space = SpaceCreate(kname, CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != space), ERR_NOMEM);
        space->max_prio = current_task->max_prio;

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, SpaceDestroy(space); SpaceFinalize(space);
               ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_SPACE;
        entry->space = space;
        entry->perms = PERM_ALL;
        SpaceRef(space);

        ArchSetInFrame(frame, 0, (Register)HANDLE_PACK(new_handle, entry->generation));
    } break;
    case OBJECT_MEMORY: {
        // MEMORY: r0=type, r1=page_count, r2=flags. Only SHM is user-creatable; Device
        // MemObjects come from kernel/boot-time injection (InjectDeviceObjectsToRootSvc
        // in boot_programs.c), never this path.
        CreateMemoryFlags flags = (CreateMemoryFlags)(*ArchGetFromFrame(frame, 2));
        ENSURE_ERR(frame, !(flags & ~MEM_FLAGS_ALL), ERR_BADARG);
        ENSURE_ERR(frame, !(flags & MEM_UNCACHED) || (flags & MEM_CONTIG), ERR_BADARG);

        size_t page_count = (size_t)(*ArchGetFromFrame(frame, 1));
        ENSURE_ERR(frame, (page_count > 0), ERR_BADARG);

        ENSURE_ERR(frame, page_count <= PmmGetStats().free_frames, ERR_NOMEM);
        ENSURE_ERR(frame, page_count <= SIZE_MAX / sizeof(PhysAddr), ERR_NOMEM);

        PhysAddr *page_addrs = KZAlloc(page_count * sizeof(PhysAddr));
        ENSURE_ERR(frame, (NULL != page_addrs), ERR_NOMEM);

        MemObject *mem = MemObjCreateShm(page_addrs, page_count, flags);
        ENSURE(NULL != mem, KFree(page_addrs); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        if (flags & MEM_CONTIG) {
            PhysAddr base = PmmAllocFramesContig(page_count);
            ENSURE(PA_NULL != base, MemObjUnref(mem); ArchSetInFrame(frame, 0, ERR_NOMEM); return);
            memset((void *)PA_TO_VA(base), 0, page_count * PAGE_SIZE);
            if (flags & MEM_UNCACHED)
                ArchCacheCleanInvalidateDcacheRange(PA_TO_VA(base), page_count * PAGE_SIZE);
            for (size_t i = 0; i < page_count; i++)
                page_addrs[i] = base + (i * PAGE_SIZE);
        }

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, MemObjUnref(mem); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
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
