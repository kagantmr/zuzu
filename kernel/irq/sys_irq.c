#include "sys_irq.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "arch/arm/include/irq.h"
#include "lib/mem.h"

extern process_t *current_process;
static irq_owner_t irq_owners[MAX_IRQS];


#define LOG_FMT(fmt) "(syscall_irq) " fmt
#include "core/log.h"

static void relay_handler(void *ctx)
{
    uint32_t irq_num = (uint32_t)(uintptr_t)ctx;
    irq_disable_line(irq_num);
    irq_owners[irq_num].pending = true;

    endpoint_t *bound_port = irq_owners[irq_num].bound_port;
    if (bound_port && !list_empty(&bound_port->receiver_queue)) {
        list_node_t *waiter_node = list_pop_front(&bound_port->receiver_queue);
        process_t *waiter = container_of(waiter_node, process_t, node);
        waiter->trap_frame->r[0] = 0;
        waiter->trap_frame->r[1] = irq_num;
        waiter->process_state = PROCESS_READY;
        waiter->blocked_endpoint = NULL;
        waiter->ipc_state = IPC_NONE;
        sched_add(waiter);
        irq_owners[irq_num].pending = false;

    }
    // If no waiter, pending flag catches it in next irq_wait()
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

void irq_claim(exception_frame_t* frame) {
    uint32_t irq_num = frame->r[0];
    if (!valid_irq(irq_num)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (!irq_owners[irq_num].owner) {
        irq_owners[irq_num] = (irq_owner_t){
            .bound_port = NULL,
            .owner = current_process,
            .pending = false
        };
        irq_register(irq_num, relay_handler, (void*)(uintptr_t)irq_num);
        irq_enable_line(irq_num);
        frame->r[0] = 0;
        return;
    }
    frame->r[0] = ERR_BUSY;
    return;
}

void irq_bind(exception_frame_t* frame) {
    uint32_t irq_num = frame->r[0];
    uint32_t port_handle = frame->r[1];
    if (!valid_irq(irq_num)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner != current_process) {
        frame->r[0] = ERR_NOPERM;
        return;
    }
    if (port_handle >= MAX_HANDLE_TABLE) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    endpoint_t *ep = current_process->handle_table[port_handle];
    if (!ep) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    irq_owners[irq_num].bound_port = ep;
    frame->r[0] = 0;
}

void irq_done(exception_frame_t* frame) {
    uint32_t irq_num = frame->r[0];
    if (!valid_irq(irq_num)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (irq_owners[irq_num].owner == current_process) {
        irq_enable_line(irq_num);
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