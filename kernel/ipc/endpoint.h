#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdint.h>
#include "lib/list.h"

#define MAX_HANDLE_TABLE 16

typedef struct {
    endpoint_t *ep;
    bool grantable;
} handle_entry_t;


typedef struct endpoint {
    list_head_t sender_queue;
    list_head_t receiver_queue;
    uint32_t owner_pid;
    list_node_t node;
    int bound_irq;
} endpoint_t;


#endif // ENDPOINT_H