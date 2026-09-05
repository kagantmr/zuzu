#include "ntfn.h"

#include "core/panic.h"
#include "kernel/proc/thread.h"
#include "kernel/sched/sched.h"

#include <assert.h>
#include <zuzu/types.h>

void NtfnWakeWaiter(NtfnObj *ntfn, ThreadWaitSlot *slot, int32_t r0_value, NtfnBits bits)
{
    Thread *waiter = slot->owner;
    if (!waiter || !waiter->trap_frame) {
        panic("NtfnWakeWaiter: queued waiter with no trap frame "
              "(ntfn=%p slot=%p owner=%p trap_frame=%p)",
              (void *)ntfn, (void *)slot, (void *)waiter,
              waiter ? (void *)waiter->trap_frame : NULL);
    }

    (*arch_reg(waiter->trap_frame, 0)) = (uint32_t)r0_value;

    uint32_t match_index = WAITANY_NO_MATCH;
    if (waiter->waitany_active) {
        for (uint32_t i = 0; i < waiter->waitany_wait_count; i++) {
            if (waiter->waitany_wait_ntfns[i] == ntfn) {
                match_index = waiter->waitany_wait_slots[i].index;
                break;
            }
        }
    }
    ThreadWaitanyClearWaits(waiter);
    ThreadWaitanyClearPortWaits(waiter);
    waiter->waitany_wait_match_index = match_index;
    waiter->waitany_wait_bits = bits;

    if (waiter->wake_deadline != 0 && waiter->timeout_node.prev && waiter->timeout_node.next) {
        list_remove(&waiter->timeout_node);
    }
    waiter->wake_deadline = 0;
    waiter->wake_reason = WAKE_IPC;
    waiter->blocked_port = NULL;
    waiter->ipc_state = IPC_NONE;
    waiter->state = READY;
    sched_add(waiter);
}

void NtfnSignal(NtfnObj *ntfn, NtfnBits bits)
{
    assert(ntfn && ntfn->alive && !(bits & (1u<<31)));
    ntfn->word |= bits;
    if (!list_empty(&ntfn->wait_queue)) {
        ListNode *node = list_pop_front(&ntfn->wait_queue); 
        ThreadWaitSlot *slot = container_of(node, ThreadWaitSlot, node);
        NtfnBits delivered = ntfn->word;
        NtfnWakeWaiter(ntfn, slot, (int32_t)delivered, delivered);
        ntfn->word = 0;
    }
}

void NtfnRefDrop(NtfnObj *ntfn) {
    if (!ntfn) return;
    ntfn->ref_count--;
    if (ntfn->ref_count == 0) {
        kfree(ntfn);
    }
}