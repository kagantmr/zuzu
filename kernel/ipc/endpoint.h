#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include <vector.h>
#include "kernel/vmm/vmm.h"

struct process;


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
    bool mapped; // is this device currently mapped?
    char compatible[32]; // DTB compatible string
    uint32_t irq;
} device_cap_t;

typedef struct {
    struct process *caller;
} reply_cap_t;

typedef enum {
    HANDLE_FREE,
    HANDLE_ENDPOINT,
    HANDLE_DEVICE,
    HANDLE_SHMEM,
    HANDLE_REPLY,
} handle_type_t;

typedef struct {
    handle_type_t type;
    bool grantable;
    uintptr_t mapped_va;
    union {
        endpoint_t  *ep;
        device_cap_t *dev;
        shmem_t      *shm;
        reply_cap_t *reply;
    };
} handle_entry_t;

DEFINE_VEC(handle, handle_entry_t);

static inline int handle_vec_find_free(handle_vec_t *handles) {
    for (uint32_t i = 1; i < handles->cap; i++) {
        if (handles->data[i].type == HANDLE_FREE)
            return i;
    }
    uint32_t old_cap = handles->cap;
    if (handle_vec_grow(handles) < 0) return -1;
    return (int)old_cap;
}

#endif // ENDPOINT_H