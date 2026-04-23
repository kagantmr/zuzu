#include "sys_irq.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "arch/arm/include/irq.h"
#include <mem.h>

extern process_t *current_process;
static irq_owner_t irq_owners[MAX_IRQS];


#define LOG_FMT(fmt) "(syscall_irq) " fmt
#include "core/log.h"

static void relay_handler(void *ctx)
{
    uint32_t irq_num = (uint32_t)(uintptr_t)ctx;
    irq_disable_line(irq_num);

    irq_owners[irq_num].pending = true;

    notification_t *ntfn = irq_owners[irq_num].bound_ntfn;
    if (ntfn && ntfn->alive) {
        ntfn->word |= (1u << (irq_num & 31));
        irq_owners[irq_num].pending = false;

        if (!list_empty(&ntfn->wait_queue)) {
            list_node_t *node = list_pop_front(&ntfn->wait_queue);
            process_t *waiter = container_of(node, process_t, node);
            waiter->trap_frame->r[0] = ntfn->word;
            ntfn->word = 0;
            waiter->process_state = PROCESS_READY;
            waiter->blocked_endpoint = NULL;
            sched_add(waiter);
        }
    } else if (ntfn && !ntfn->alive) {
        irq_owners[irq_num].bound_ntfn = NULL;
    }
}

static inline bool irq_is_reserved(uint32_t irq_num)
{
    switch (irq_num) {
    case 34:   // SP804 timer
    case 27:   // generic timer PPI
        return true;
    default:
        return false;
    }
}

static inline bool valid_irq(uint32_t irq_num) {
    return (irq_num < MAX_IRQS) && !irq_is_reserved(irq_num);
}

void irq_claim(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];

    if (handle_idx == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        frame->r[0] = ERR_BADFORM;
        return;
    }

    device_cap_t *cap = entry->dev;
    uint32_t irq_num = cap->irq;

    if (!valid_irq(irq_num)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner) {
        frame->r[0] = ERR_BUSY;
        return;
    }

    irq_owners[irq_num] = (irq_owner_t){
        .bound_ntfn = NULL,
        .owner = current_process,
        .pending = false
    };
    irq_register(irq_num, relay_handler, (void*)(uintptr_t)irq_num);
    frame->r[0] = 0;
}

void irq_bind(exception_frame_t *frame) {
    uint32_t dev_handle  = frame->r[0];
    uint32_t ntfn_handle = frame->r[1];   // was port_handle

    if (dev_handle == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, dev_handle);
    if (!entry || entry->type != HANDLE_DEVICE) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    uint32_t irq_num = entry->dev->irq;
    if (!valid_irq(irq_num)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner != current_process) {
        frame->r[0] = ERR_NOPERM;
        return;
    }

    handle_entry_t *ntfn_entry = handle_vec_get(&current_process->handle_table, ntfn_handle);
    if (!ntfn_entry || ntfn_entry->type != HANDLE_NOTIFICATION || !ntfn_entry->ntfn) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (!ntfn_entry->ntfn->alive) {
        frame->r[0] = ERR_DEAD;
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
            process_t *waiter = container_of(node, process_t, node);
            waiter->trap_frame->r[0] = ntfn->word;
            ntfn->word = 0;
            waiter->process_state = PROCESS_READY;
            waiter->blocked_endpoint = NULL;
            sched_add(waiter);
        }
    }

    irq_enable_line(irq_num);
    frame->r[0] = 0;
}

void irq_done(exception_frame_t* frame) {
    uint32_t dev_handle  = frame->r[0];

    if (dev_handle == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, dev_handle);
    if (!entry) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        frame->r[0] = ERR_BADFORM;
        return;
    }
    if (!valid_irq(entry->dev->irq)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (irq_owners[entry->dev->irq].owner == current_process) {
        irq_enable_line(entry->dev->irq);
        frame->r[0] = 0;
        return;
    } else {
        frame->r[0] = ERR_NOPERM;
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
            irq_disable_line(i);
            irq_unregister(i);
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