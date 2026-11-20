/**
 * @file space.c
 * @brief Implementation of spaces: creation, teardown and lookup.
 */

#include "space.h"

#include "kernel/irq/irq_relay.h"
#include "kernel/lbx/lbx.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/sched/sched.h"
#include "kernel/checkpoint/checkpoint.h"
#include "kernel/syspage.h"
#include "kernel/task/task.h"
#include <arch/mmu.h>
#include <string.h>
#include <zuzu/user_layout.h>

#define LOG_FMT(fmt) "(space) " fmt
#include "core/ensure.h"
#include "core/log.h"

/** @brief The ID the next space will try to take. */
static uint32_t next_spid = 1;
/** @brief Live spaces, indexed by ID modulo MAX_SPACES. */
static SpaceObject *spaces[MAX_SPACES];
/** @brief Allocator for space objects. */
static KSlabCache space_cache;
/** @brief Frames collected by one teardown step; lbx is the only user. */
static PhysAddr space_reap_scratch[CHECKPOINT_PAGES];

SpaceObject kernel_space;

/** @brief Move to the next space ID, wrapping to 1 before it can go negative. */
static void SpidAdvance(void)
{
    next_spid++;
    if (next_spid == 0 || next_spid > (uint32_t)INT32_MAX)
        next_spid = 1;
}

/**
 * @brief Pick an ID for a new space and register it in the table.
 *
 * @param[in] sp The space being created.
 * @return The ID, or 0 if every slot is taken.
 */
static Spid SpidAlloc(SpaceObject *sp)
{
    uint32_t start = next_spid % MAX_SPACES;
    uint32_t slot = start;
    do {
        if (spaces[slot] == NULL)
            break;
        SpidAdvance();
        slot = next_spid % MAX_SPACES;
    } while (slot != start);

    if (spaces[slot] != NULL)
        return 0;

    Spid spid = (Spid)next_spid;
    SpidAdvance();
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

    ListInit(&sp->tasks);
    ListInit(&sp->waiters);

    HandleTableInit(&sp->handle_table);
    sp->as = AddressSpaceCreate(ADDRESS_SPACE_USER);
    if (!sp->as)
        goto fail_handles;

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
        .owner = VM_BACKING_ANON,
        .flags = VM_FLAG_PINNED,
    };
    if (!VmmAddRegion(sp->as, &tcb_region))
        goto fail_as;

    sp->tcb_page_va = tcb_user_va;

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

    memset((void *)PA_TO_VA(tcb_page0_phys_addr), 0, PAGE_SIZE);
    memset(sp->tcb_slot_bitmap, 0, sizeof(sp->tcb_slot_bitmap));

    Spid spid = SpidAlloc(sp);
    if (!spid)
        goto fail_as;
    sp->spid = spid;

    if (name) {
        const char *short_name = name;
        for (const char *ch = name; *ch; ch++) {
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

/**
 * @brief Kill the ports and events a space owns, failing their waiters.
 *
 * @param[in] sp The space being torn down.
 */
static void SpaceKillOwned(SpaceObject *sp)
{
    for (size_t i = BitmapNextSet(sp->handle_table.slot_bitmap, HANDLE_MAX_SLOTS, 0);
         i < HANDLE_MAX_SLOTS;
         i = BitmapNextSet(sp->handle_table.slot_bitmap, HANDLE_MAX_SLOTS, i + 1)) {
        HandleTableEntry *entry = HandleTableGet(&sp->handle_table, (Handle)i);
        if (!entry)
            continue;
        if (entry->type == HANDLE_PORT && entry->port && entry->port->owner_spid == sp->spid)
            PortKill(entry->port);
        else if (entry->type == HANDLE_EVENT && entry->event &&
                 entry->event->owner_spid == sp->spid)
            EventKill(entry->event);
    }
}

/**
 * @brief Runs one step of a torn-down space's current reclaim phase: destroys its tasks, then
 * frees its address space, handles and tables.
 *
 * @param[in,out] sp The space.
 * @param[in] budget The most tasks, pages or handles to reclaim.
 * @return LBX_MORE if work is left, LBX_DONE once the teardown reference is dropped.
 */
static LbxResult SpaceLbxPhase(SpaceObject *sp, uint32_t budget)
{
    switch (sp->reap_phase) {
    case SPACE_REAP_TASKS:
        for (; budget > 0 && !ListIsEmpty(&sp->tasks); budget--)
            TaskDestroy(container_of(sp->tasks.node.next, TaskObject, space_node));
        if (ListIsEmpty(&sp->tasks))
            sp->reap_phase = SPACE_REAP_REGIONS;
        return LBX_MORE;

    case SPACE_REAP_REGIONS: {
        size_t pages = budget < CHECKPOINT_PAGES ? budget : CHECKPOINT_PAGES;
        if (sp->as && VmmTeardownChunk(sp->as, space_reap_scratch, pages))
            return LBX_MORE;
        sp->reap_phase = SPACE_REAP_HANDLES;
        sp->reap_pos = 0;
        return LBX_MORE;
    }

    case SPACE_REAP_HANDLES:
        for (; budget > 0; budget--) {
            size_t i = BitmapNextSet(sp->handle_table.slot_bitmap, HANDLE_MAX_SLOTS, sp->reap_pos);
            if (i >= HANDLE_MAX_SLOTS) {
                sp->reap_phase = SPACE_REAP_TABLES;
                return LBX_MORE;
            }
            sp->reap_pos = (uint32_t)i + 1;
            HandleTableEntry *entry = HandleTableGet(&sp->handle_table, (Handle)i);
            if (entry && entry->type != HANDLE_FREE)
                HandleRelease(sp, entry);
        }
        return LBX_MORE;

    case SPACE_REAP_TABLES:
        if (sp->as) {
            if (AddressSpaceDestroyChunk(sp->as, 1))
                return LBX_MORE;
            sp->as = NULL;
        }
        HandleTableDestroy(&sp->handle_table);
        SpaceUnref(sp);
        return LBX_DONE;
    }
    return LBX_MORE;
}

/**
 * @brief lbx step for a torn-down space; runs the current phase.
 *
 * @param[in,out] n The space's lbx node.
 * @param[in] budget The most tasks, pages or handles to reclaim.
 * @return LBX_MORE if work is left, LBX_DONE once the teardown reference is dropped.
 */
static LbxResult SpaceLbxStep(LbxNode *n, uint32_t budget)
{
    return SpaceLbxPhase(container_of(n, SpaceObject, lbx_node), budget);
}

void SpaceDestroy(SpaceObject *sp)
{
    if (!sp || sp->torn_down || SpaceIsKernel(sp))
        return;
    sp->torn_down = true;
    sp->ref_count++;

    ListNode *task_node = sp->tasks.node.next;
    while (task_node != &sp->tasks.node) {
        ListNode *next = task_node->next;
        TaskObject *task = container_of(task_node, TaskObject, space_node);
        if (task->state != TASK_STATE_ZOMBIE)
            TaskTerminate(task, ERR_DEAD);
        task_node = next;
    }

    while (!ListIsEmpty(&sp->waiters)) {
        ListNode *node = ListPopFront(&sp->waiters);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }

    IrqReleaseAll(sp);
    SpaceKillOwned(sp);

    uint32_t slot = (uint32_t)sp->spid % MAX_SPACES;
    if (spaces[slot] == sp)
        spaces[slot] = NULL;

    sp->reap_phase = SPACE_REAP_TASKS;
    LbxQueue(&sp->lbx_node, SpaceLbxStep);
}

void SpaceFinalize(SpaceObject *sp)
{
    if (!sp)
        return;
    if (!sp->torn_down || !ListIsEmpty(&sp->tasks) || sp->ref_count != 0)
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
        if (t->state == TASK_STATE_READY && !t->node.next)
            SchedAdd(t);
        n = n->next;
    }
}

void SpaceFaultCleared(SpaceObject *owner)
{
    if (!owner)
        return;
    if (owner->faulted_tasks > 0)
        owner->faulted_tasks--;
    if (owner->faulted_tasks == 0)
        SpaceUnfreeze(owner);
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
    if (sp->live_tasks == 0) {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)sp->last_exit_status);
        return;
    }
    SchedBlockOn(&sp->waiters, timeout);
}

bool SpaceIsHollow(const SpaceObject *sp) { return sp->live_tasks == 0; }
bool SpaceIsSelfOrAncestor(const SpaceObject *sp, const SpaceObject *target)
{
    for (uint32_t depth = 0; sp && depth < MAX_SPACES; depth++) {
        if (sp == target)
            return true;
        sp = sp->parent_spid == -1 ? NULL : SpaceFindBySpid(sp->parent_spid);
    }
    return false;
}

void SpaceInitKernelSpace(void)
{
    memset(&kernel_space, 0, sizeof(kernel_space));
    ListInit(&kernel_space.tasks);
    ObserverInit(&kernel_space.observers);
    HandleTableInit(&kernel_space.handle_table);
    ListInit(&kernel_space.waiters);
    kernel_space.as = VmmGetKernelAddressSpace();
    kernel_space.spid = 0;
    kernel_space.parent_spid = -1;
    strncpy(kernel_space.name, "zuzu", 4);
    kernel_space.max_prio = SCHED_PRIORITY_LEVELS - 1;
    kernel_space.ref_count = 1; // important
}
