#ifndef SYS_IRQ_H
#define SYS_IRQ_H

#include <arch/regs.h>
#include "kernel/ipc/ntfn.h"
#include "kernel/ipc/port.h"
#include "stdbool.h"

typedef struct process process_t;

typedef struct irq_owner {
    process_t      *owner;
    bool            pending;
    Ntfn *bound_ntfn;   // was Endpoint *bound_port
} irq_owner_t;

void sys_irq_bind(CpuState* frame);
void sys_irq_done(CpuState* frame);
void irq_release_all(process_t *owner);

bool irq_check_and_clear_pending(int irq_num);

const irq_owner_t *irq_panic_owners(void);



#endif // SYS_IRQ_H