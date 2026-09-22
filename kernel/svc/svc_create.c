#include "svc.h"
#include <zuzu/types.h>
#include <zuzu/err.h>
#include "core/ensure.h"
#include "kernel/space/space.h"
#include <arch/regs.h>

void SvcCreate(CpuState *frame)
{
    // Dispatch based on type
    CreateType type = (CreateType)(*ArchGetFromFrame(frame, 0));
    switch (type) {
        case CREATE_TASK: {
            // TASK: r0=type, r1=space_handle (must be a kitten of its parent)
            Handle space_handle = (Handle)(*ArchGetFromFrame(frame, 1));
            HandleTableEntry *space_entry = HandleTableLookup(&CURRENT_SPACE->handle_table, space_handle);

            ENSURE_ERR(frame, (NULL != space_entry), ERR_BADHANDLE);
            ENSURE_ERR(frame, (HANDLE_SPACE == space_entry->type), ERR_BADTYPE);
            ENSURE_ERR(frame, (NULL != space_entry->space), ERR_BADHANDLE);

            TaskObject *task = TaskCreate(space_entry->space);

            ENSURE_ERR(frame, (NULL != task), ERR_BUSY);

            Handle new_handle = HandleTableFindFree(&CURRENT_SPACE->handle_table);
            ENSURE_ERR(frame, (-1 != new_handle), ERR_NOMEM);

            HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, (uint32_t)new_handle);
            HandleEntryClaim(&CURRENT_SPACE->handle_table, entry);
            entry->type = HANDLE_TASK;
            entry->task = task;

            (*ArchGetFromFrame(frame, 0)) = (Register)HANDLE_PACK(new_handle, entry->generation);
        } break;
        case CREATE_PORT: {
            
        } break;
        case CREATE_EVENT: {

        } break;
        case CREATE_SPACE: {

        } break;
        case CREATE_MEMORY: {

        } break;
        default: ENSURE_ERR(frame, 0, ERR_BADARG);
    }
}
