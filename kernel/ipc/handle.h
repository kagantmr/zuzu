#ifndef KERNEL_HANDLE_H
#define KERNEL_HANDLE_H

#include <zuzu/types.h>
#include <stdint.h>
#include <list.h>
#include <vector.h>

#include "kernel/mm/vmm.h"

#include "ntfn.h"
#include "port.h"
#include "kernel/dev/devcap.h"

typedef enum {
    HANDLE_FREE,
    HANDLE_ENDPOINT,
    HANDLE_DEVICE,
    HANDLE_SHMEM,
    HANDLE_REPLY,
    HANDLE_NOTIFICATION,
    HANDLE_TASK
} HandleType;

typedef struct {
    HandleType type;
    bool grantable;
    VirtAddr mapped_va;
    union {
        Port  *port;
        DeviceCap *dev;
        shmem_t      *shm;
        ReplyCap *reply;
        Notification *ntfn;
        struct process *task;
    };
    uint32_t marker;
} HandleEntry;

DEFINE_VEC(handle, HandleEntry)

static inline int handle_vec_find_free(handle_vec_t *handles) {
    for (uint32_t i = 1; i < handles->cap; i++) {
        if (handles->data[i].type == HANDLE_FREE)
            return i;
    }
    uint32_t old_cap = handles->cap;
    if (handle_vec_grow(handles) < 0) return -1;
    return (int)old_cap;
}

#endif 