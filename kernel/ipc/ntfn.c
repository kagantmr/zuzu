#include "ntfn.h"

#include "core/panic.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/thread.h"
#include "kernel/sched/sched.h"

#include <assert.h>
#include <zuzu/types.h>

static KHeapSlabCache ntfn_cache;

EventObject *KAllocNtfn(void)
{
    if (!ntfn_cache.obj_size)
        KSlabInit(&ntfn_cache, "NtfnObj", sizeof(EventObject));
    return KSlabAlloc(&ntfn_cache);
}

void KFreeNtfn(EventObject *ntfn) { KSlabFree(&ntfn_cache, ntfn); }

void NtfnWakeWaiter(EventObject *ntfn, WaitSlot *slot, int32_t r0_value)
{
    TaskObject *waiter = slot->owner;
    if (!waiter || !waiter->trap_frame) {
        panic("NtfnWakeWaiter: queued waiter with no trap frame "
              "(ntfn=%p slot=%p owner=%p trap_frame=%p)",
              (void *)ntfn, (void *)slot, (void *)waiter,
              waiter ? (void *)waiter->trap_frame : NULL);
    }

    (*ArchGetFromFrame(waiter->trap_frame, 0)) = (uint32_t)r0_value;

    SchedRemoveSleepQueue(waiter);
    waiter->wake_deadline = 0;
    waiter->wake_reason = WAKE_IPC;
    waiter->blocked_port = NULL;
    waiter->ipc_state = IPC_NONE;
    waiter->state = READY;
    SchedAdd(waiter);
}

void NtfnSignal(EventObject *ntfn, NtfnBits bits)
{
    assert(ntfn && ntfn->alive && !(bits & (1u<<31)));
    ntfn->word |= bits;
    if (!list_empty(&ntfn->wait_queue)) {
        ListNode *node = list_pop_front(&ntfn->wait_queue);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        NtfnBits delivered = ntfn->word;
        NtfnWakeWaiter(ntfn, slot, (int32_t)delivered);
        ntfn->word = 0;
    }
}

void NtfnRefDrop(EventObject *ntfn) {
    if (!ntfn) return;
    ntfn->ref_count--;
    if (ntfn->ref_count == 0) {
        KFreeNtfn(ntfn);
    }
}