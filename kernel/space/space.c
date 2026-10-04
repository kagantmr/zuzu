#include "space.h"

#include "kernel/irq/irq_relay.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/sched/sched.h"
#include "kernel/syspage.h"
#include "kernel/task/task.h"
#include <arch/mmu.h>
#include <string.h>
#include <zuzu/user_layout.h>

#define LOG_FMT(fmt) "(space) " fmt
#include "core/ensure.h"
#include "core/log.h"

static uint32_t next_spid = 1;
static SpaceObject *spaces[MAX_SPACES];
static KSlabCache space_cache;

static Spid SpidAlloc(SpaceObject *sp)
{
    uint32_t start = next_spid % MAX_SPACES;
    uint32_t slot = start;
    do
    {
        if (spaces[slot] == NULL)
            break;
        next_spid++;
        slot = next_spid % MAX_SPACES;
    } while (slot != start);

    if (spaces[slot] != NULL)
        return 0;

    Spid spid = (Spid)next_spid++;
    spaces[slot] = sp;
    return spid;
}

SpaceObject *SpaceCreate(const char *name, const SpaceObject *parent)
{
    if (!space_cache.obj_size)
        KSlabInit(&space_cache, sizeof(SpaceObject));
    SpaceObject *sp = KSlabAlloc(&space_cache);
    if (!sp)
        return NULL;
    memset(sp, 0, sizeof(*sp));
    ObserverInit(&sp->observers);
    sp->parent_spid = parent ? parent->spid : -1;

    list_init(&sp->tasks);
    list_init(&sp->waiters);

    HandleTableInit(&sp->handle_table);
    sp->as = AddressSpaceCreate(ADDRESS_SPACE_USER);
    if (!sp->as)
        goto fail_handles;

    /* Map syspage into user space. */
    if (!VmmMapUserPage(sp->as, SyspagePa(), USER_SYSPAGE_VA, PROT_READ))
        goto fail_as;

    VirtMemRegion sys_region = {
        .vaddr_start = USER_SYSPAGE_VA,
        .size = PAGE_SIZE,
        .prot = PROT_READ | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_BACKING_SHARED,
        .flags = VM_FLAG_PINNED | VM_FLAG_GUARD,
    };
    if (!VmmAddRegion(sp->as, &sys_region))
        goto fail_as;

    PhysAddr tcb_page0_phys_addr = PmmAllocFrame();
    if (!tcb_page0_phys_addr)
        goto fail_as;
    sp->tcb_page_pa[0] = tcb_page0_phys_addr;

    VirtAddr tcb_user_va =
        VmmFindFreeVa(sp->as, USER_MMAP_BASE, USER_DEVICE_BASE, MAX_TCB_PAGES * PAGE_SIZE);
    if (!tcb_user_va)
        goto fail_as;
    if (!VmmMapUserPage(sp->as, tcb_page0_phys_addr, tcb_user_va,
                        VM_PROT_USER | PROT_READ | PROT_WRITE))
        goto fail_as;

    VirtMemRegion tcb_region = {
        .vaddr_start = tcb_user_va,
        .size = MAX_TCB_PAGES * PAGE_SIZE,
        .prot = PROT_READ | PROT_WRITE | VM_PROT_USER,
        .owner = VM_BACKING_ANON, // GUARD dropped so pages 1..N demand-back; PINNED still blocks user
                                // unmap.
        .flags = VM_FLAG_PINNED,
    };
    if (!VmmAddRegion(sp->as, &tcb_region))
        goto fail_as;

    sp->tcb_page_va = tcb_user_va; /* user-visible VA */

    VirtMemRegion stack_region = {
        .vaddr_start = USER_STACK_BASE,
        .size = USER_STACK_TOP - USER_STACK_BASE,
        .prot = PROT_READ | PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_BACKING_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!VmmAddRegion(sp->as, &stack_region))
        goto fail_as;

    VirtMemRegion stack_guard = {
        .vaddr_start = USER_STACK_GUARD_VA,
        .size = PAGE_SIZE,
        .prot = 0,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_BACKING_NONE,
        .flags = VM_FLAG_GUARD,
    };
    if (!VmmAddRegion(sp->as, &stack_guard))
        goto fail_as;

    /* Initialize the TCB page via the kernel alias; slot claiming for the
     * first task happens in TaskCreate, not here. */
    memset((void *)PA_TO_VA(tcb_page0_phys_addr), 0, PAGE_SIZE);
    memset(sp->tcb_slot_bitmap, 0, sizeof(sp->tcb_slot_bitmap));

    Spid spid = SpidAlloc(sp);
    if (!spid)
        goto fail_as;
    sp->spid = spid;

    if (name)
    {
        const char *short_name = name;
        for (const char *ch = name; *ch; ch++)
        {
            if (*ch == '/')
                short_name = ch + 1;
        }
        strncpy(sp->name, short_name, sizeof(sp->name) - 1);
    }

    return sp;

fail_as:
    if (sp->as)
        ArchMmuFreeUserPages(sp->as);
    AddressSpaceDestroy(sp->as);
    memset(sp->tcb_page_pa, 0, sizeof(sp->tcb_page_pa));
fail_handles:
    HandleTableDestroy(&sp->handle_table);
    KSlabFree(&space_cache, sp);
    return NULL;
}

SpaceObject *SpaceFindBySpid(Spid spid)
{
    uint32_t slot = (uint32_t)spid % MAX_SPACES;
    SpaceObject *sp = spaces[slot];
    if (sp && sp->spid == spid)
        return sp;
    return NULL;
}

void SpaceDestroy(SpaceObject *sp)
{
    if (!sp || sp->torn_down)
        return;
    sp->torn_down = true;
    sp->ref_count++;

    ListNode *task_node = sp->tasks.node.next;
    while (task_node != &sp->tasks.node)
    {
        ListNode *next = task_node->next;
        TaskObject *task = container_of(task_node, TaskObject, space_node);
        TaskRef(task);
        if (task->state != ZOMBIE)
            TaskTerminate(task, ERR_DEAD);
        if (task != current_task)
            TaskDestroy(task);
        else
            SchedQueueDestroyTask(task);
        TaskUnref(task);
        task_node = next;
    }

    while (!list_empty(&sp->waiters))
    {
        ListNode *node = list_pop_front(&sp->waiters);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }

    IrqReleaseAll(sp);

    for (uint32_t i = 0; i < HANDLE_MAX_SLOTS; i++)
    {
        HandleTableEntry *entry = HandleTableGet(&sp->handle_table, (Handle)i);
        if (!entry || entry->type == HANDLE_FREE)
            continue;
        if (entry->type == HANDLE_PORT && entry->port && entry->port->owner_spid == sp->spid)
            PortKill(entry->port);
        else if (entry->type == HANDLE_EVENT && entry->event && entry->event->owner_spid == sp->spid)
            EventKill(entry->event);
        HandleRelease(sp, entry);
    }

    if (sp->as)
    {
        ArchMmuFreeUserPages(sp->as);
        AddressSpaceDestroy(sp->as);
        sp->as = NULL;
    }
    HandleTableDestroy(&sp->handle_table);

    uint32_t slot = (uint32_t)sp->spid % MAX_SPACES;
    if (spaces[slot] == sp)
        spaces[slot] = NULL;

    SpaceUnref(sp);
}

void SpaceFinalize(SpaceObject *sp)
{
    if (!sp)
        return;
    if (!sp->torn_down || !list_empty(&sp->tasks) || sp->ref_count != 0)
        return;
    ObserverClear(&sp->observers);
    KSlabFree(&space_cache, sp);
}

void SpaceUnfreeze(SpaceObject *owner)
{
    if (!owner)
        return;
    owner->frozen = false;

    ListNode *n = owner->tasks.node.next;
    while (n != &owner->tasks.node) {
        TaskObject *t = container_of(n, TaskObject, space_node);
        if (t->state == READY && !t->node.next)
            SchedAdd(t);
        n = n->next;
    }
}

void SpaceRef(SpaceObject *sp)
{
    if (!sp)
        return;
    sp->ref_count++;
}

void SpaceUnref(SpaceObject *sp)
{
    if (!sp)
        return;
    if (sp->ref_count > 0)
        sp->ref_count--;
    SpaceFinalize(sp);
}

void SpaceWaitHollow(SpaceObject *sp, Duration timeout, CpuState *frame)
{
    ENSURE_ERR(frame, sp != current_task->owner, ERR_BADARG);
    ENSURE_ERR(frame, !sp->torn_down, ERR_DEAD);
    if (sp->live_tasks == 0)
    {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)sp->last_exit_status);
        return;
    }
    SchedBlockOn(&sp->waiters, timeout);
}

bool SpaceIsHollow(const SpaceObject *sp)
{
    return sp->live_tasks == 0;
}
bool SpaceIsSelfOrAncestor(const SpaceObject *sp, const SpaceObject *target)
{
    /* Bounded: a dead ancestor's spid can be reused by an unrelated space. */
    for (uint32_t depth = 0; sp && depth < MAX_SPACES; depth++)
    {
        if (sp == target)
            return true;
        sp = sp->parent_spid == -1 ? NULL : SpaceFindBySpid(sp->parent_spid);
    }
    return false;
}
