#include "core/ensure.h"
#include "kernel/ipc/port.h"
#include "kernel/space/space.h"
#include "svc.h"
#include <arch/regs.h>
#include <zuzu/err.h>
#include <zuzu/types.h>

static void FreeScatteredPages(PhysAddr *addrs, size_t count)
{
    for (size_t i = 0; i < count; i++)
        PmmFreeFrame(addrs[i]);
    KFree(addrs);
}

void SvcCreate(CpuState *frame)
{
    // Dispatch based on type
    CreateType type = (CreateType)(*ArchGetFromFrame(frame, 0));
    switch (type)
    {
    case CREATE_TASK:
    {
        // TASK: r0=type, r1=space_handle (a kitten Space you hold a handle
        // to, or -1 to spawn a sibling Task in your own Space)
        Handle space_handle = (Handle)(*ArchGetFromFrame(frame, 1));
        SpaceObject *target_space;

        if (-1 == space_handle)
        {
            target_space = CURRENT_SPACE;
        }
        else
        {
            HandleTableEntry *space_entry =
                HandleTableLookup(&CURRENT_SPACE->handle_table, space_handle);

            ENSURE_ERR(frame, (NULL != space_entry), ERR_BADHANDLE);
            ENSURE_ERR(frame, (HANDLE_SPACE == space_entry->type), ERR_BADTYPE);
            ENSURE_ERR(frame, (NULL != space_entry->space), ERR_BADHANDLE);
            target_space = space_entry->space;
        }

        TaskObject *task = TaskCreate(target_space);

        ENSURE_ERR(frame, (NULL != task), ERR_BUSY);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, TaskDestroy(task); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry =
            HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_TASK;
        entry->task = task;

        (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
    }
    break;
    case CREATE_PORT:
    {
        // PORT: r0=type
        PortObject *new_port = PortCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_port), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, PortDestroy(new_port); ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry =
            HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_PORT;
        entry->port = new_port;

        (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
    }
    break;
    case CREATE_EVENT:
    {
        // EVENT: r0=type
        EventObject *new_event = EventCreate(CURRENT_SPACE);
        ENSURE_ERR(frame, (NULL != new_event), ERR_NOMEM);
    
        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, EventDestroy(new_event); ArchSetInFrame(frame, 0, ERR_NOMEM); return);
    
        HandleTableEntry *entry =
            HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_EVENT;
        entry->event = new_event;
    
        (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
    }
    break;
    case CREATE_SPACE:
    {
        // SPACE: r0=type, r1=name_ptr, r2=name_len
        VirtAddr name_ptr = (VirtAddr)(*ArchGetFromFrame(frame, 1));
        size_t name_len = (size_t)(*ArchGetFromFrame(frame, 2));

        char kname[32]; // matches SpaceObject.name[32]
        if (name_len >= sizeof(kname))
            name_len = sizeof(kname) - 1;

        ENSURE_ERR(frame, (name_len == 0 || CopyFromUser(kname, (const void *)name_ptr, name_len)),
                   ERR_BADPTR);
        kname[name_len] = '\0';

        SpaceObject *space = SpaceCreate(kname);
        ENSURE_ERR(frame, (NULL != space), ERR_NOMEM);

        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, SpaceDestroy(space); SpaceFinalize(space);
               ArchSetInFrame(frame, 0, ERR_NOMEM); return);

        HandleTableEntry *entry =
            HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_SPACE;
        entry->space = space;

        (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
    }
    break;
    case CREATE_MEMORY:
    {
        // MEMORY: r0=type, r1=page_count. Only SHM is user-creatable; Device
        // MemObjects come from kernel/boot-time injection (InjectDeviceObjectsToRootSvc
        // in boot_programs.c), never this path.
        size_t page_count = (size_t)(*ArchGetFromFrame(frame, 1));
        ENSURE_ERR(frame, (page_count > 0), ERR_BADARG);
    
        PhysAddr *page_addrs = KZAlloc(page_count * sizeof(PhysAddr));
        ENSURE_ERR(frame, (NULL != page_addrs), ERR_NOMEM);
    
        size_t got = PmmAllocFramesScattered(page_count, page_addrs);
        ENSURE(got == page_count, FreeScatteredPages(page_addrs, got);
               ArchSetInFrame(frame, 0, ERR_NOMEM); return);
    
        MemObject *mem = MemObjCreateShm(page_addrs, page_count);
        ENSURE(NULL != mem, FreeScatteredPages(page_addrs, page_count);
               ArchSetInFrame(frame, 0, ERR_NOMEM); return);
    
        Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE(-1 != new_handle, MemObjDestroy(mem); ArchSetInFrame(frame, 0, ERR_NOMEM); return);
    
        HandleTableEntry *entry =
            HandleTableGet(&CURRENT_SPACE->handle_table, new_handle);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
        entry->type = HANDLE_MEM;
        entry->mem = mem;
        entry->grantable = true;
        entry->mapped_va = 0;
    
        (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
    }
    break;
    default:
        ENSURE_ERR(frame, 0, ERR_BADARG);
    }
}