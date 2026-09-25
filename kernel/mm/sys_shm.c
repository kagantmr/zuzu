#include "sys_shm.h"

#include "kernel/sched/sched.h"
#include <arch/cache.h>
#include <arch/mmu.h>
#include <string.h>

#include "core/log.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"

extern TaskObject *current_task;

void ShmemDropReference(ShmObject *shm)
{
    if (!shm)
        return;
    if (shm->ref_count > 0)
        shm->ref_count--;
    if (shm->ref_count == 0) {
        for (size_t j = 0; j < shm->page_count; j++)
            if (shm->page_addrs[j] != 0) /* demand-paged: skip unfaulted slots */
                PmmFreeFrame(shm->page_addrs[j]);
        KFree(shm->page_addrs);
        KFree(shm);
    }
}

void SysShmCreate(CpuState *frame)
{
    const size_t size = align_up((size_t)(*ArchGetFromFrame(frame, 0)), PAGE_SIZE);
    if (size == 0)
    {
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }
    if (size > 1024 * 1024 * 32)
    {
        ArchSetInFrame(frame, 0, ERR_OVERFLOW); // 32mb static cap
        return;
    }
    const size_t page_count = size / PAGE_SIZE;
    PhysAddr *page_arr = KCalloc(page_count, sizeof(PhysAddr));
    if (!page_arr)
    {
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    ShmObject *shmem_obj = KZAlloc(sizeof(ShmObject));
    if (!shmem_obj)
    {
        KFree(page_arr);
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }
    shmem_obj->page_count = page_count;
    /* ref_count counts live HANDLE references, not mappings. The creating
     * handle installed below holds the first reference; each grant adds one,
     * each destroy/teardown drops one. Mappings never touch it. */
    shmem_obj->ref_count = 1;
    shmem_obj->page_addrs = page_arr;

    HandleTable *ht = &current_task->owner_process->handle_table;
    int handle = HandleTableFindFree(ht);
    if (handle < 0)
    {
        KFree(page_arr);
        KFree(shmem_obj);
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    HandleTableEntry *entry = HandleTableGet(ht, (uint32_t)handle);
    if (!entry)
    {
        KFree(page_arr);
        KFree(shmem_obj);
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    entry->mapped_va = 0;
    entry->shm = shmem_obj;
    entry->type = HANDLE_SHM;
    entry->grantable = true;
    HandleEntryClaim(ht, entry);

    ArchSetInFrame(frame, 0, (Handle)handle);
}
