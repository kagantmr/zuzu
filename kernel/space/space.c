#include "space.h"
#include "kernel/mm/alloc.h"

uint32_t next_pid = 1;
SpaceObject *spaces[MAX_SPACES];
static KHeapSlabCache space_cache;

SpaceObject *CreateSpace(const char *name)
{
    SpaceObject *sp = KSlabAlloc(&space_cache);
    if (!sp) return NULL;
    memset(sp, 0, sizeof(*sp));

    list_init(&sp->tasks);
    list_init(&sp->children);

    if (!HandleTableInit(&sp->handle_table))
        goto fail;
    sp->as = AddrspaceCreate(ADDRSPACE_USER);
    if (!sp->as)
        goto fail_handles;

    sp->pid = AllocSpid();
    strncpy(sp->name, name, sizeof(sp->name) - 1);
    return sp;
fail_handles:
    HandleTableDestroy(&sp->handle_table);
fail:
    KSlabFree(&space_cache, sp);
    return NULL;
}