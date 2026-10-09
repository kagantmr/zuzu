#include "core/ensure.h"
#include "kernel/mm/mem_object.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/space/space.h"
#include "svc.h"
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
            ENSURE_ERR(frame, entry, ERR_BADHANDLE);
            ENSURE_ERR(frame, (HANDLE_MEM == entry->type), ERR_BADTYPE);
            ENSURE_ERR(frame, entry->perms & PERM_MAP, ERR_NOPERM);
            Err rc = VmmMapMemObj(CURRENT_SPACE, entry, prot, hint, &out);
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
    case MNGMEM_INJECTOBJ: {
        Handle kitten_space_handle = (*ArchGetFromFrame(frame, 1));

        HandleTableEntry *entry =
            HandleTableLookup(&CURRENT_SPACE->handle_table, kitten_space_handle);
        ENSURE_ERR(frame, entry, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_SPACE == entry->type), ERR_BADTYPE);
        ENSURE_ERR(frame, !entry->space->torn_down, ERR_DEAD);
        ENSURE_ERR(frame, entry->perms & PERM_CNTL, ERR_NOPERM);

        InjectObjArgs kargs;
        ENSURE_ERR(frame,
                   CopyFromUser(&kargs, (const void *)(*ArchGetFromFrame(frame, 2)), sizeof(kargs)),
                   ERR_BADPTR);

        ArchSetInFrame(frame, 0, InjectObjIntoSpace(entry->space, CURRENT_SPACE, &kargs));
    } break;
    case MNGMEM_DMAMAP: {
        DmaMapArgs kargs;
        ENSURE_ERR(frame,
                   CopyFromUser(&kargs, (const void *)(*ArchGetFromFrame(frame, 1)), sizeof(kargs)),
                   ERR_BADPTR);

        HandleTableEntry *dev = HandleTableLookup(&CURRENT_SPACE->handle_table, kargs.dev);
        ENSURE_ERR(frame, dev, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_MEM == dev->type), ERR_BADTYPE);
        ENSURE_ERR(frame, dev->perms & PERM_MAP, ERR_NOPERM);

        HandleTableEntry *mem = HandleTableLookup(&CURRENT_SPACE->handle_table, kargs.mem);
        ENSURE_ERR(frame, mem, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_MEM == mem->type), ERR_BADTYPE);
        ENSURE_ERR(frame, mem->perms & PERM_MAP, ERR_NOPERM);

        uintptr_t bus_addr = 0;
        uint32_t id = 0;
        Err rc =
            MemObjDmaMap(dev->mem, mem->mem, kargs.offset, kargs.len, kargs.dir, &bus_addr, &id);
        ArchSetInFrame(frame, 0, rc);
        ArchSetInFrame(frame, 1, (rc == ZUZU_OK) ? (Register)bus_addr : 0);
        ArchSetInFrame(frame, 2, (rc == ZUZU_OK) ? (Register)id : 0);
    } break;
    case MNGMEM_DMAUNMAP: {
        Handle dev_handle = (*ArchGetFromFrame(frame, 1));
        uint32_t id = (uint32_t)(*ArchGetFromFrame(frame, 2));

        HandleTableEntry *dev = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);
        ENSURE_ERR(frame, dev, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_MEM == dev->type), ERR_BADTYPE);
        ENSURE_ERR(frame, dev->perms & PERM_MAP, ERR_NOPERM);

        ArchSetInFrame(frame, 0, MemObjDmaUnmap(dev->mem, id));
    } break;
    case MNGMEM_DMAADOPT: {
        Handle dev_handle = (*ArchGetFromFrame(frame, 1));
        uint32_t id = (uint32_t)(*ArchGetFromFrame(frame, 2));

        HandleTableEntry *dev = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);
        ENSURE_ERR(frame, dev, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_MEM == dev->type), ERR_BADTYPE);
        ENSURE_ERR(frame, dev->mem && dev->mem->kind == MEMKIND_DEVICE, ERR_BADTYPE);
        ENSURE_ERR(frame, dev->perms & PERM_MAP, ERR_NOPERM);

        const DmaMapping *m = MemObjDmaFind(dev->mem, id);
        ENSURE_ERR(frame, m, ERR_NOENT);

        Handle slot = HandleTableFindFree(&CURRENT_SPACE->handle_table);
        ENSURE_ERR(frame, -1 != slot, ERR_NOMEM);
        HandleTableEntry *fresh = HandleTableGet(&CURRENT_SPACE->handle_table, slot);
        HandleEntryClaim(&CURRENT_SPACE->handle_table, fresh);
        MemObjRef(m->mem);
        fresh->type = HANDLE_MEM;
        fresh->mem = m->mem;
        fresh->perms = PERM_MAP;
        fresh->mapped_va = 0;

        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)HANDLE_PACK(slot, fresh->generation));
    } break;
    case MNGMEM_DMASYNC: {
        Handle dev_handle = (*ArchGetFromFrame(frame, 1));
        uintptr_t raw = (uintptr_t)(*ArchGetFromFrame(frame, 2));
        size_t len = (size_t)(*ArchGetFromFrame(frame, 3));
        DmaSyncOp op = (DmaSyncOp)(raw & 1U);
        uintptr_t bus_addr = raw & ~(uintptr_t)1U;

        HandleTableEntry *dev = HandleTableLookup(&CURRENT_SPACE->handle_table, dev_handle);
        ENSURE_ERR(frame, dev, ERR_BADHANDLE);
        ENSURE_ERR(frame, (HANDLE_MEM == dev->type), ERR_BADTYPE);
        ENSURE_ERR(frame, dev->perms & PERM_MAP, ERR_NOPERM);

        ArchSetInFrame(frame, 0, MemObjDmaSync(dev->mem, bus_addr, len, op));
    } break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
    }
}
