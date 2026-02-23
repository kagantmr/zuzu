#ifndef SYS_IRQ_H
#define SYS_IRQ_H

#include "arch/arm/include/context.h"
#include "stdbool.h"

typedef struct process process_t;

void irq_claim(exception_frame_t* frame);
void irq_wait(exception_frame_t* frame);
void irq_done(exception_frame_t* frame);

void irq_release_all(process_t *owner);

typedef struct irq_owner {
    process_t *owner;
    bool pending;
    process_t *blocked;
} irq_owner_t;

#endif // SYS_IRQ_H