#ifndef SYS_IRQ_H
#define SYS_IRQ_H

#include "arch/arm/include/context.h"
#include "kernel/ipc/endpoint.h"
#include "stdbool.h"

typedef struct process process_t;

typedef struct irq_owner {
    process_t *owner;
    bool pending;
    endpoint_t *bound_port;
} irq_owner_t;

void irq_claim(exception_frame_t* frame);
void irq_bind(exception_frame_t* frame);
void irq_done(exception_frame_t* frame);
void irq_release_all(process_t *owner);

bool irq_check_and_clear_pending(int irq_num);



#endif // SYS_IRQ_H