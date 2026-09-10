#ifndef KERNEL_HANDLE_H
#define KERNEL_HANDLE_H

#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/types.h>

#include "kernel/mm/vmm.h"
#include "kernel/mm/alloc.h"

#include "kernel/dev/devcap.h"
#include "ntfn.h"
#include "port.h"

#define GRANT_REGRANTABLE (1u << 0)

#define HANDLE_TABLE_INITIAL_CAP 16u

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
        NtfnObj *ntfn;
        struct process *task;
    };
    Marker marker; /* Added in zuzu 1.1: Same handle can be stamped with a marker to demux clients
                      in waitany() */
} HandleEntry;

/* Fixed-purpose slot table: dense integer-indexed HandleEntry array plus an
 * occupancy bitmap so FindFree scans 32 slots per word instead of walking
 * entries. slot_bitmap bit i set <=> slot i is allocated, and must stay in
 * sync with data[i].type (HANDLE_FREE iff the bit is clear). */
typedef struct
{
    HandleEntry *data;
    uint32_t cap;
    uint32_t *slot_bitmap; /* BITMAP_WORDS(cap) words */
} HandleTable;

static inline bool HandleTableInit(HandleTable *t)
{
    t->cap = HANDLE_TABLE_INITIAL_CAP;
    t->data = KZAlloc(t->cap * sizeof(HandleEntry));
    t->slot_bitmap = KZAlloc(BITMAP_WORDS(t->cap) * sizeof(uint32_t));
    if (!t->data || !t->slot_bitmap)
    {
        KFree(t->data);
        KFree(t->slot_bitmap);
        t->data = NULL;
        t->slot_bitmap = NULL;
        t->cap = 0;
        return false;
    }
    return true;
}

static inline void HandleTableDestroy(HandleTable *t)
{
    KFree(t->data);
    KFree(t->slot_bitmap);
    t->data = NULL;
    t->slot_bitmap = NULL;
    t->cap = 0;
}

static __always_inline HandleEntry *HandleTableGet(HandleTable *t, uint32_t i)
{
    if (unlikely(i >= t->cap))
        return NULL;
    return &t->data[i];
}

static inline int HandleTableGrow(HandleTable *t)
{
    uint32_t new_cap = t->cap * 2U;
    HandleEntry *new_data = KZAlloc(new_cap * sizeof(HandleEntry));
    uint32_t *new_bitmap = KZAlloc(BITMAP_WORDS(new_cap) * sizeof(uint32_t));
    if (!new_data || !new_bitmap)
    {
        KFree(new_data);
        KFree(new_bitmap);
        return -1;
    }
    memcpy(new_data, t->data, t->cap * sizeof(HandleEntry));
    memcpy(new_bitmap, t->slot_bitmap, BITMAP_WORDS(t->cap) * sizeof(uint32_t));
    KFree(t->data);
    KFree(t->slot_bitmap);
    t->data = new_data;
    t->slot_bitmap = new_bitmap;
    t->cap = new_cap;
    return 0;
}

static inline int HandleTableFindFree(HandleTable *t)
{
    int slot = BitmapFindFirstZero(t->slot_bitmap, t->cap);
    if (slot >= 0)
        return slot;

    uint32_t old_cap = t->cap;
    if (HandleTableGrow(t) < 0)
        return -1;
    return (int)old_cap;
}

/* Mark a slot allocated. Call once, after every error check has passed and
 * the entry's type/union are filled in -- a FindFree slot that is never
 * claimed stays free, so a failed object alloc after FindFree leaks nothing. */
static inline void HandleEntryClaim(HandleTable *t, HandleEntry *e)
{
    BitmapSet(t->slot_bitmap, (size_t)(e - t->data));
}

static inline void HandleEntryFree(HandleTable *t, HandleEntry *e)
{
    BitmapClr(t->slot_bitmap, (size_t)(e - t->data));
    memset(e, 0, sizeof(*e));
}


#endif
