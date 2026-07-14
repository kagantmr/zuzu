#include "sys_irq.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include <arch/irq.h>
#include <mem.h>

extern thread_t *current_thread;
static irq_owner_t irq_owners[MAX_IRQS];


#define LOG_FMT(fmt) "(syscall_irq) " fmt
#include "core/log.h"

static void relay_handler(void *ctx)
{
    irq_t irq_num = (irq_t)(vaddr_t)ctx;
    arch_irq_disable_line(irq_num);

    irq_owners[irq_num].pending = true;

    notification_t *ntfn = irq_owners[irq_num].bound_ntfn;
    if (ntfn && ntfn->alive) {
        ntfn->word |= (1u << (irq_num & 31));
        irq_owners[irq_num].pending = false;

        if (!list_empty(&ntfn->wait_queue)) {
            list_node_t *node = list_pop_front(&ntfn->wait_queue);
            thread_wait_slot_t *slot = container_of(node, thread_wait_slot_t, node);
            thread_t *waiter = slot->owner;
            if (!waiter->trap_frame)
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
            thread_waitany_clear_waits(waiter);
            thread_waitany_clear_ep_waits(waiter);
            waiter->waitany_wait_match_index = match_index;
            waiter->waitany_wait_bits = ntfn->word;
            if (waiter->wake_tick != 0 && waiter->timeout_node.prev && waiter->timeout_node.next) {
                list_remove(&waiter->timeout_node);
            }
            waiter->wake_tick = 0;
            ntfn->word = 0;
            waiter->wake_reason = WAKE_IPC;
            waiter->blocked_endpoint = NULL;
            waiter->ipc_state = IPC_NONE;
            waiter->state = READY;
            sched_add(waiter);
            if (!current_thread || waiter->priority > current_thread->priority) {
                do_resched = 1;
            }
        }
    } else if (ntfn && !ntfn->alive) {
        irq_owners[irq_num].bound_ntfn = NULL;
    }
}

static inline bool valid_irq(irq_t irq_num) {
    return (irq_num < MAX_IRQS) && !arch_irq_is_reserved(irq_num);
}

void sys_irq_claim(arch_regs_t *frame) {
    uint32_t handle_idx = (*arch_reg(frame, 0));

    if (handle_idx == 0) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }

    device_cap_t *cap = entry->dev;
    irq_t irq_num = cap->irq;

    if (!valid_irq(irq_num)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner) {
        (*arch_reg(frame, 0)) = ERR_BUSY;
        return;
    }

    irq_owners[irq_num] = (irq_owner_t){
        .bound_ntfn = NULL,
        .owner = current_thread->owner_process,
        .pending = false
    };
    arch_irq_register(irq_num, relay_handler, (void*)(vaddr_t)irq_num);
    (*arch_reg(frame, 0)) = 0;
}

void sys_irq_bind(arch_regs_t *frame) {
    handle_t dev_handle  = (*arch_reg(frame, 0));
    handle_t ntfn_handle = (*arch_reg(frame, 1));   // was port_handle

    if (dev_handle == 0) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, dev_handle);
    if (!entry) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }

    irq_t irq_num = entry->dev->irq;
    if (!valid_irq(irq_num)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner != current_thread->owner_process) {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }

    handle_entry_t *ntfn_entry = handle_vec_get(&current_thread->owner_process->handle_table, ntfn_handle);
    if (!ntfn_entry || !ntfn_entry->ntfn) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (ntfn_entry->type != HANDLE_NOTIFICATION) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }

    if (!ntfn_entry->ntfn->alive) {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return;
    }

    if (irq_owners[irq_num].bound_ntfn) {
        notification_t *old = irq_owners[irq_num].bound_ntfn;
        if (old->ref_count > 0)
            old->ref_count--;
        if (old->ref_count == 0)
            kfree(old);
    }

    irq_owners[irq_num].bound_ntfn = ntfn_entry->ntfn;
    irq_owners[irq_num].bound_ntfn->ref_count++;

    if (irq_owners[irq_num].pending) {
        notification_t *ntfn = irq_owners[irq_num].bound_ntfn;
        ntfn->word |= (1u << (irq_num & 31));
        irq_owners[irq_num].pending = false;

        if (!list_empty(&ntfn->wait_queue)) {
            list_node_t *node = list_pop_front(&ntfn->wait_queue);
            thread_wait_slot_t *slot = container_of(node, thread_wait_slot_t, node);
            thread_t *waiter = slot->owner;
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
            thread_waitany_clear_waits(waiter);
            thread_waitany_clear_ep_waits(waiter);
            waiter->waitany_wait_match_index = match_index;
            waiter->waitany_wait_bits = ntfn->word;
            if (waiter->wake_tick != 0 && waiter->timeout_node.prev && waiter->timeout_node.next) {
                list_remove(&waiter->timeout_node);
            }
            waiter->wake_tick = 0;
            ntfn->word = 0;
            waiter->wake_reason = WAKE_IPC;
            waiter->blocked_endpoint = NULL;
            waiter->ipc_state = IPC_NONE;
            waiter->state = READY;
            sched_add(waiter);
        }
    }

    arch_irq_enable_line(irq_num);
    (*arch_reg(frame, 0)) = 0;
}

void sys_irq_done(arch_regs_t* frame) {
    handle_t dev_handle  = (*arch_reg(frame, 0));

    if (dev_handle == 0) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, dev_handle);
    if (!entry) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }
    if (!valid_irq(entry->dev->irq)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (irq_owners[entry->dev->irq].owner == current_thread->owner_process) {
        arch_irq_enable_line(entry->dev->irq);
        (*arch_reg(frame, 0)) = 0;
        return;
    } else {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }
}

void irq_release_all(process_t *owner) {
    for (int i = 0; i < MAX_IRQS; i++) {
        if (irq_owners[i].owner == owner) {
            if (irq_owners[i].bound_ntfn) {
                notification_t *ntfn = irq_owners[i].bound_ntfn;
                if (ntfn->ref_count > 0)
                    ntfn->ref_count--;
                if (ntfn->ref_count == 0)
                    kfree(ntfn);
                irq_owners[i].bound_ntfn = NULL;
            }
            arch_irq_disable_line(i);
            arch_irq_unregister(i);
            memset(&irq_owners[i], 0, sizeof(irq_owner_t));
        }
    }
}

bool irq_check_and_clear_pending(int irq_num) {
    if (irq_num < 0 || irq_num >= MAX_IRQS) return false;
    if (irq_owners[irq_num].pending) {
        irq_owners[irq_num].pending = false;
        return true;
    }
    return false;
}

const irq_owner_t *irq_panic_owners(void) { return irq_owners; }
