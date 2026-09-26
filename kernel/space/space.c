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

static uint32_t next_pid = 1;
static SpaceObject *spaces[MAX_SPACES];
static KHeapSlabCache space_cache;

static Spid SpidAlloc(SpaceObject *sp)
{
    uint32_t start = next_pid % MAX_SPACES;
    uint32_t slot = start;
    do
    {
        if (spaces[slot] == NULL)
            break;
        next_pid++;
        slot = next_pid % MAX_SPACES;
    } while (slot != start);

    if (spaces[slot] != NULL)
        return 0;

    Spid pid = (Spid)next_pid++;
    spaces[slot] = sp;
    return pid;
}

SpaceObject *SpaceCreate(const char *name)
{
    if (!space_cache.obj_size)
        KSlabInit(&space_cache, "SpaceObject", sizeof(SpaceObject));
    SpaceObject *sp = KSlabAlloc(&space_cache);
    if (!sp)
        return NULL;
    memset(sp, 0, sizeof(*sp));

    list_init(&sp->tasks);
    list_init(&sp->kittens);
    list_init(&sp->waiters);

    if (!HandleTableInit(&sp->handle_table))
        goto fail;
    sp->as = AddrspaceCreate(ADDRSPACE_USER);
    if (!sp->as)
        goto fail_handles;

    /* Map syspage into user space. */
    if (!VmmMapUserPage(sp->as, SyspagePhysAddr(), USER_SYSPAGE_VA, PROT_READ))
        goto fail_as;

    VirtMemRegion sys_region = {
        .vaddr_start = USER_SYSPAGE_VA,
        .size = PAGE_SIZE,
        .prot = PROT_READ | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
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
        .owner = VM_OWNER_ANON, // GUARD dropped so pages 1..N demand-back; PINNED still blocks user
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
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!VmmAddRegion(sp->as, &stack_region))
        goto fail_as;

    VirtMemRegion stack_guard = {
        .vaddr_start = USER_STACK_GUARD_VA,
        .size = PAGE_SIZE,
        .prot = 0,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_NONE,
        .flags = VM_FLAG_GUARD,
    };
    if (!VmmAddRegion(sp->as, &stack_guard))
        goto fail_as;

    /* Initialize the TCB page via the kernel alias; slot claiming for the
     * first task happens in TaskCreate, not here. */
    memset((void *)PA_TO_VA(tcb_page0_phys_addr), 0, PAGE_SIZE);
    memset(sp->tcb_slot_bitmap, 0, sizeof(sp->tcb_slot_bitmap));

    Spid pid = SpidAlloc(sp);
    if (!pid)
        goto fail_as;
    sp->spid = pid;

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
        arch_mmu_free_user_pages(sp->as);
    AddrspaceDestroy(sp->as);
    memset(sp->tcb_page_pa, 0, sizeof(sp->tcb_page_pa));
fail_handles:
    HandleTableDestroy(&sp->handle_table);
fail:
    KSlabFree(&space_cache, sp);
    return NULL;
}

SpaceObject *SpaceFindBySpid(Spid pid)
{
    uint32_t slot = (uint32_t)pid % MAX_SPACES;
    SpaceObject *sp = spaces[slot];
    if (sp && sp->spid == pid)
        return sp;
    return NULL;
}

void SpaceReparent(SpaceObject *kitten, SpaceObject *parent)
{
    if (!kitten)
        return;

    if (kitten->sibling_node.prev && kitten->sibling_node.next)
        list_remove(&kitten->sibling_node);

    kitten->parent_spid = parent ? parent->spid : 0;

    if (parent)
        list_add_tail(&kitten->sibling_node, &parent->kittens.node);
}

SpaceObject *SpaceFindKittenBySpid(SpaceObject *parent, Spid pid)
{
    if (!parent)
        return NULL;

    ListNode *node = parent->kittens.node.next;
    while (node != &parent->kittens.node)
    {
        SpaceObject *child = container_of(node, SpaceObject, sibling_node);
        if (child->spid == pid)
            return child;
        node = node->next;
    }

    return NULL;
}

SpaceObject *SpaceFindHollowKitten(SpaceObject *parent)
{
    if (!parent)
        return NULL;

    ListNode *node = parent->kittens.node.next;
    while (node != &parent->kittens.node)
    {
        SpaceObject *child = container_of(node, SpaceObject, sibling_node);
        if (list_empty(&child->tasks))
            return child;
        node = node->next;
    }

    return NULL;
}

SpaceObject *SpaceFindZombieKitten(SpaceObject *parent)
{
    if (!parent)
        return NULL;

    ListNode *node = parent->kittens.node.next;
    while (node != &parent->kittens.node)
    {
        SpaceObject *child = container_of(node, SpaceObject, sibling_node);
        if (child->main_task && child->main_task->state == ZOMBIE)
            return child;
        node = node->next;
    }

    return NULL;
}

void SpaceDestroy(SpaceObject *sp)
{
    if (!sp || sp->torn_down)
        return;
    sp->torn_down = true;

    ListNode *task_node = sp->tasks.node.next;
    while (task_node != &sp->tasks.node)
    {
        ListNode *next = task_node->next;
        TaskObject *task = container_of(task_node, TaskObject, space_node);
        if (task->state != ZOMBIE)
            TaskTerminate(task, ERR_DEAD);
        task_node = next;
    }

    while (!list_empty(&sp->waiters))
    {
        ListNode *node = list_pop_front(&sp->waiters);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }

    IrqReleaseAll(sp);
    if (sp->node.prev && sp->node.next)
        list_remove(&sp->node);
    if (sp->destroy_node.prev && sp->destroy_node.next)
        list_remove(&sp->destroy_node);
    if (sp->timeout_node.prev && sp->timeout_node.next)
        list_remove(&sp->timeout_node);

    ListNode *child_node = sp->kittens.node.next;
    while (child_node != &sp->kittens.node)
    {
        ListNode *next = child_node->next;
        SpaceObject *child = container_of(child_node, SpaceObject, sibling_node);
        SpaceDestroy(child);
        child_node = next;
    }
    if (sp->sibling_node.prev && sp->sibling_node.next)
        list_remove(&sp->sibling_node);

    for (uint32_t i = 0; i < HANDLE_MAX_SLOTS; i++)
    {
        HandleTableEntry *entry = HandleTableGet(&sp->handle_table, (Handle)i);
        if (!entry)
            continue;

        if (entry->type == HANDLE_PORT)
        {
            PortObject *port = entry->port;
            if (port && port->owner_spid == sp->spid && port->alive)
            {
                port->alive = false;
                while (!list_empty(&port->sender_queue))
                {
                    ListNode *n = list_pop_front(&port->sender_queue);
                    TaskObject *task = container_of(n, TaskObject, node);
                    TaskAbortWait(task, ERR_DEAD);
                }
                while (!list_empty(&port->receiver_queue))
                {
                    ListNode *n = list_pop_front(&port->receiver_queue);
                    WaitSlot *slot = container_of(n, WaitSlot, node);
                    TaskObject *task = slot->owner;
                    TaskAbortWait(task, ERR_DEAD);

                }
            }
            if (port)
            {
                if (port->ref_count > 0)
                    port->ref_count--;
                if (port->ref_count == 0)
                    PortObjFree(port);
            }
            HandleEntryFree(&sp->handle_table, entry);
        }
        else if (entry->type == HANDLE_MEM)
        {
            MemObject *mem = entry->mem;
            if (mem)
            {
                if (entry->mapped_va != 0)
                {
                    size_t region_size = (mem->kind == MEMTYPE_DEVICE)
                        ? mem->dev.size
                        : mem->shm.page_count * PAGE_SIZE;
                    VmmRemoveRegion(sp->as, entry->mapped_va, region_size);
                }
                MemObjDestroy(mem);
            }
            HandleEntryFree(&sp->handle_table, entry);
        }
        else if (entry->type == HANDLE_EVENT)
        {
            EventObject *event = entry->event;
            if (event && event->owner_spid == sp->spid && event->alive)
            {
                event->alive = false;
                while (!list_empty(&event->wait_queue))
                {
                    ListNode *n = list_pop_front(&event->wait_queue);
                    WaitSlot *slot = container_of(n, WaitSlot, node);
                    TaskAbortWait(slot->owner, ERR_DEAD);
                }
            }
            if (event)
                EventDropReference(event);
            HandleEntryFree(&sp->handle_table, entry);
        }
        else if (entry->type == HANDLE_TASK || entry->type == HANDLE_SPACE)
        {
            /* No kernel object owned by this space's teardown: the
             * referenced Task/Space outlives this handle. */
            HandleEntryFree(&sp->handle_table, entry);
        }
    }

    if (sp->as)
    {
        arch_mmu_free_user_pages(sp->as);
        AddrspaceDestroy(sp->as);
        sp->as = NULL;
    }
    HandleTableDestroy(&sp->handle_table);

    uint32_t slot = (uint32_t)sp->spid % MAX_SPACES;
    if (spaces[slot] == sp)
        spaces[slot] = NULL;

    if (list_empty(&sp->tasks))
        SpaceFinalize(sp);
}

void SpaceFinalize(SpaceObject *sp)
{
    if (!sp)
        return;
    KSlabFree(&space_cache, sp);
}

void SpaceWaitHollow(SpaceObject *sp, Duration timeout, CpuState *frame)
{
    ENSURE_ERR(frame, sp != current_task->owner, ERR_BADARG);
    if (sp->live_tasks == 0)
    {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)sp->last_exit_status);
        return;
    }
    SchedBlockOn(&sp->waiters, timeout);
}
