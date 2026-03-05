#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdint.h>
#include "lib/list.h"

#define MAX_HANDLE_TABLE 16

typedef struct endpoint {
    list_head_t sender_queue;
    list_head_t receiver_queue;
    uint32_t owner_pid;
    list_node_t node;
    int bound_irq;
} endpoint_t;

// stub
typedef struct {
    uint32_t phys_base;
    uint32_t size;
} device_cap_t;

// stub
typedef struct {
    uint32_t id;
} shmem_t;

typedef enum {
    HANDLE_FREE,
    HANDLE_ENDPOINT,
    HANDLE_DEVICE,
    HANDLE_SHMEM,
} handle_type_t;

typedef struct {
    handle_type_t type;
    bool grantable;
    union {
        endpoint_t  *ep;
        device_cap_t *dev;
        shmem_t      *shm;
    };
} handle_entry_t;


#endif // ENDPOINT_H