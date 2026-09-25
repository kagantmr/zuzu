#include "svc.h"
#include "kernel/space/space.h"
#include "kernel/mm/vmm/vmm.h"
#include "core/ensure.h"
#include <arch/regs.h>

void SvcManageMemory(CpuState *frame)
{
    ManageMemoryVerb verb = (ManageMemoryVerb)(*ArchGetFromFrame(frame, 0));
    switch (verb) {
        case MNGMEM_MAP: {
            VirtAddr out;
            Handle mem_handle = (*ArchGetFromFrame(frame, 1));
            uint32_t hint_prot = (uint32_t)(*ArchGetFromFrame(frame, 3));
            // (hint_va & ~0xFFF) | (prot & 0x7)
            MemProt prot = (hint_prot & 0x7);
            VirtAddr hint = (hint_prot & (unsigned)~(0xFFF));

            if (mem_handle == HANDLE_ANON) {
                size_t size = (size_t)(*ArchGetFromFrame(frame, 2));

                Err rc = VmmMapAnon(CURRENT_SPACE, hint, size, prot, &out);
                ArchSetInFrame(frame, 0, (rc == 0) ? (signed)out : rc);
            } else {
                HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, mem_handle);
                ENSURE_ERR(frame, entry, ERR_BADARG);
                ENSURE_ERR(frame,(HANDLE_MEM == entry->type), ERR_BADARG);
                Err rc = VmmMapMemObject(CURRENT_SPACE, entry, prot, hint, &out);
                ArchSetInFrame(frame, 0, (rc == 0) ? (signed)out : rc);
            }
        } break;
        case MNGMEM_UNMAP: {
            VirtAddr va = (VirtAddr)(*ArchGetFromFrame(frame, 1));
            ArchSetInFrame(frame, 0, VmmUnmapUserRegion(CURRENT_SPACE, va));
        } break;
        case MNGMEM_PROTECT: {
            VirtAddr va = (VirtAddr)(*ArchGetFromFrame(frame, 1));
            size_t size = (size_t)(*ArchGetFromFrame(frame, 2));
            MemProt new_prot = (MemProt)(*ArchGetFromFrame(frame, 3));
            
            ArchSetInFrame(frame, 0, VmmProtectUserRange(CURRENT_SPACE, va, size, new_prot));
        } break;
        case MNGMEM_INJECT: {

            Handle kitten_space_handle = (*ArchGetFromFrame(frame, 1));

            HandleTableEntry *entry = HandleTableLookup(&CURRENT_SPACE->handle_table, kitten_space_handle);
            ENSURE_ERR(frame, entry, ERR_BADARG);
            ENSURE_ERR(frame,(HANDLE_SPACE == entry->type), ERR_BADARG);

            InjectArgs kargs;
            ENSURE_ERR(frame, CopyFromUser(&kargs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(kargs)), ERR_BADPTR);

            ArchSetInFrame(frame, 0, InjectInKittenSpace(entry->space, CURRENT_SPACE, &kargs));
        } break;
        default: ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}
