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

#define GRANT_REGRANTABLE (1u << 0)

typedef enum
{
    HANDLE_FREE,
    HANDLE_PORT,
    HANDLE_DEVICE,
    HANDLE_SHM,
    HANDLE_REPLY,
    HANDLE_NTFN,
    HANDLE_TASK
} HandleType;

typedef struct
{
    HandleType type;    /* HANDLE_* */
    bool grantable;     /* Will grant() work on this handle? */
    VirtAddr mapped_va; /* For shm and device: destroy() checks before freeing */
    union
    {
        Port *port;
        DeviceCap *dev;
        ShmCap *shm;
        ReplyCap *reply;
        Ntfn *ntfn;
        struct process *task;
    };
    Marker marker;      /* Added in zuzu 1.1: Same handle can be stamped with a marker to demux clients in waitany() */
} HandleEntry;

DEFINE_VEC(handle, HandleEntry)

/**
 * @brief Inline helper to find a free spot.
 * 
 * @param handles Vector of handles.
 * 
 * @return Index of free spot
 * 
 * zuzu 1.0 Loaf used a handle vector O(n), and inlining reduces perforamance
 * todo: change data structure in zuzu 2.0 Prowl
 */
static inline int handle_vec_find_free(handle_vec_t *handles)
{
    for (uint32_t i = 0; i < handles->cap; i++)
    {
        if (handles->data[i].type == HANDLE_FREE)
            return i;
    }
    uint32_t old_cap = handles->cap;
    if (handle_vec_grow(handles) < 0)
        return -1;
    return (int)old_cap;
}

#endif
