#include "sys_irq.h"
#include "kernel/bench.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include <arch/irq.h>
#include <compiler.h>
#include <string.h>

extern Thread *current_thread;
static IrqOwner irq_owners[MAX_IRQS];

#ifdef ZUZU_BENCH
BENCH_STAT(g_bench_irq_wait, "IRQ wait block->unblock");
#endif

#define LOG_FMT(fmt) "(syscall_irq) " fmt
#include "core/log.h"

/* Runs in interrupt context on every IRQ this process owns -- a driver's
 * hottest function by definition. A device with no live, bound, waited-on
 * notification is the misconfigured/shutdown-race case, not the steady
 * state, so all three guards below are marked unlikely-to-bail. */
static void __hot relay_handler(void *ctx)
{
    Irq irq_num = (Irq)(VirtAddr)ctx;
    arch_irq_disable_line(irq_num);

    irq_owners[irq_num].pending = true;

    NtfnObj *ntfn = irq_owners[irq_num].bound_ntfn;
    if (likely(ntfn && ntfn->alive)) {
        ntfn->word |= (1u << (irq_num & 31));
        irq_owners[irq_num].pending = false;

        if (likely(!list_empty(&ntfn->wait_queue))) {
            ListNode *node = list_pop_front(&ntfn->wait_queue);
            ThreadWaitSlot *slot = container_of(node, ThreadWaitSlot, node);
            Thread *waiter = slot->owner;
            if (unlikely(!waiter->trap_frame))
                return;
            (*arch_reg(waiter->trap_frame, 0)) = ntfn->word;
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
            waiter->waitany_wait_bits = ntfn->word;
            if (unlikely(waiter->wake_tick != 0 && waiter->timeout_node.prev &&
                         waiter->timeout_node.next)) {
                list_remove(&waiter->timeout_node);
            }
            waiter->wake_tick = 0;
            ntfn->word = 0;
            waiter->wake_reason = WAKE_IPC;
            waiter->blocked_port = NULL;
            waiter->ipc_state = IPC_NONE;
            waiter->state = READY;
#ifdef ZUZU_BENCH
            BENCH_END(g_bench_irq_wait, waiter->bench_irq_wait_start);
#endif
            sched_add(waiter);
            if (!current_thread || waiter->priority > current_thread->priority) {
                do_resched = 1;
            }
        }
    } else if (ntfn && !ntfn->alive) {
        irq_owners[irq_num].bound_ntfn = NULL;
    }
}

static inline bool valid_irq(Irq irq_num)
{
    return (irq_num < MAX_IRQS) && !arch_irq_is_reserved(irq_num);
}

/*
 * ZuzuIrqBind: claim ownership of a device's IRQ line (if not already owned by
 * the caller) and bind a notification to it in a single syscall. Formerly two
 * syscalls, irq_claim + ZuzuIrqBind, which every caller invoked back-to-back.
 */
void SysIrqBind(CpuState *frame)
{
    Handle dev_handle = (Handle)(*arch_reg(frame, 0));
    Handle ntfn_handle = (Handle)(*arch_reg(frame, 1));

    if (dev_handle == 0) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)dev_handle);
    if (!entry) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }

    Irq irq_num = entry->dev->irq;
    if (!valid_irq(irq_num)) {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    /* Ownership: free line is ours to claim; a line owned by someone else is busy. */
    Process *owner = irq_owners[irq_num].owner;
    if (owner && owner != current_thread->owner_process) {
        arch_reg_set(frame, 0, ERR_BUSY);
        return;
    }

    /* Validate the notification before mutating any state so a bad ntfn handle
     * does not leave the line claimed-but-unbound. */
    HandleEntry *ntfn_entry =
        handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)ntfn_handle);
    if (!ntfn_entry || !ntfn_entry->ntfn) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (ntfn_entry->type != HANDLE_NTFN) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }
    if (!ntfn_entry->ntfn->alive) {
        arch_reg_set(frame, 0, ERR_DEAD);
        return;
    }

    /* Claim the line on first bind. */
    if (!owner) {
        irq_owners[irq_num] = (IrqOwner){ .bound_ntfn = NULL,
                                          .owner = current_thread->owner_process,
                                          .pending = false };
        arch_irq_register(irq_num, relay_handler, (void *)(VirtAddr)irq_num);
    }

    if (irq_owners[irq_num].bound_ntfn) {
        NtfnObj *old = irq_owners[irq_num].bound_ntfn;
        if (old->ref_count > 0)
            old->ref_count--;
        if (old->ref_count == 0)
            kfree(old);
    }

    irq_owners[irq_num].bound_ntfn = ntfn_entry->ntfn;
    irq_owners[irq_num].bound_ntfn->ref_count++;

    if (irq_owners[irq_num].pending) {
        NtfnObj *ntfn = irq_owners[irq_num].bound_ntfn;
        ntfn->word |= (1u << (irq_num & 31));
        irq_owners[irq_num].pending = false;

        if (!list_empty(&ntfn->wait_queue)) {
            ListNode *node = list_pop_front(&ntfn->wait_queue);
            ThreadWaitSlot *slot = container_of(node, ThreadWaitSlot, node);
            Thread *waiter = slot->owner;
            if (waiter->trap_frame)
                (*arch_reg(waiter->trap_frame, 0)) = ntfn->word;
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
            waiter->waitany_wait_bits = ntfn->word;
            if (unlikely(waiter->wake_tick != 0 && waiter->timeout_node.prev &&
                         waiter->timeout_node.next)) {
                list_remove(&waiter->timeout_node);
            }
            waiter->wake_tick = 0;
            ntfn->word = 0;
            waiter->wake_reason = WAKE_IPC;
            waiter->blocked_port = NULL;
            waiter->ipc_state = IPC_NONE;
            waiter->state = READY;
            sched_add(waiter);
        }
    }

    arch_irq_enable_line(irq_num);
    (*arch_reg(frame, 0)) = 0;
}

void SysIrqDone(CpuState *frame)
{
    Handle dev_handle = (Handle)(*arch_reg(frame, 0));

    if (dev_handle == 0) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)dev_handle);
    if (!entry) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }
    if (!entry->dev) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (!valid_irq(entry->dev->irq)) {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }
    if (irq_owners[entry->dev->irq].owner == current_thread->owner_process) {
        arch_irq_enable_line(entry->dev->irq);
        (*arch_reg(frame, 0)) = 0;
        return;
    } else {
        arch_reg_set(frame, 0, ERR_NOPERM);
        return;
    }
}

void IrqReleaseAll(Process *owner)
{
    for (int i = 0; i < MAX_IRQS; i++) {
        if (irq_owners[i].owner == owner) {
            if (irq_owners[i].bound_ntfn) {
                NtfnObj *ntfn = irq_owners[i].bound_ntfn;
                if (ntfn->ref_count > 0)
                    ntfn->ref_count--;
                if (ntfn->ref_count == 0)
                    kfree(ntfn);
                irq_owners[i].bound_ntfn = NULL;
            }
            arch_irq_disable_line((uint32_t)i);
            arch_irq_unregister((uint32_t)i);
            memset(&irq_owners[i], 0, sizeof(IrqOwner));
        }
    }
}

bool IrqClearPending(int irq_num)
{
    if (irq_num < 0 || irq_num >= MAX_IRQS)
        return false;
    if (irq_owners[irq_num].pending) {
        irq_owners[irq_num].pending = false;
        return true;
    }
    return false;
}

const IrqOwner *GetIrqOwners(void)
{
    return irq_owners;
}
