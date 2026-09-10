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

#define HANDLE_BLOCK_SLOTS 64U
#define HANDLE_MAX_BLOCKS  16U
#define HANDLE_MAX_SLOTS   (HANDLE_BLOCK_SLOTS * HANDLE_MAX_BLOCKS) /* 1024 */

typedef HandleEntry HandleBlock[HANDLE_BLOCK_SLOTS];

typedef struct
{
    HandleBlock *blocks[HANDLE_MAX_BLOCKS];
    uint32_t slot_bitmap[BITMAP_WORDS(HANDLE_MAX_SLOTS)];
} HandleTable;

static inline bool HandleTableInit(HandleTable *t)
{
    memset(t, 0, sizeof(*t));
    return true;
}

static inline void HandleTableDestroy(HandleTable *t)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++)
    {
        KFree(t->blocks[b]);
        t->blocks[b] = NULL;
    }
    BitmapZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
}

/* Bounds check + two-level index, hit on every send/recv/call/reply/notify/
 * irq/memmap syscall. One extra branch + deref over a flat array. */
static __always_inline HandleEntry *HandleTableGet(HandleTable *t, uint32_t i)
{
    if (unlikely(i >= HANDLE_MAX_SLOTS))
        return NULL;
    HandleBlock *blk = t->blocks[i / HANDLE_BLOCK_SLOTS];
    if (unlikely(!blk))
        return NULL;
    return &(*blk)[i % HANDLE_BLOCK_SLOTS];
}

/* Returns a free slot index, allocating its leaf block on first use. */
static inline int HandleTableFindFree(HandleTable *t)
{
    int slot = BitmapFindFirstZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
    if (slot < 0)
        return -1;

    uint32_t b = (uint32_t)slot / HANDLE_BLOCK_SLOTS;
    if (!t->blocks[b])
    {
        t->blocks[b] = KZAlloc(sizeof(HandleBlock));
        if (!t->blocks[b])
            return -1;
    }
    return slot;
}


static inline HandleEntry *HandleTableGetOrAlloc(HandleTable *t, uint32_t i)
{
    if (i >= HANDLE_MAX_SLOTS)
        return NULL;
    uint32_t b = i / HANDLE_BLOCK_SLOTS;
    if (!t->blocks[b])
    {
        t->blocks[b] = KZAlloc(sizeof(HandleBlock));
        if (!t->blocks[b])
            return NULL;
    }
    return &(*t->blocks[b])[i % HANDLE_BLOCK_SLOTS];
}

/* Recover a slot index from a HandleEntry * by finding its leaf block. */
static inline uint32_t HandleEntryIndex(const HandleTable *t, const HandleEntry *e)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++)
    {
        const HandleEntry *base = t->blocks[b] ? (*t->blocks[b]) : NULL;
        if (base && e >= base && e < base + HANDLE_BLOCK_SLOTS)
            return (b * HANDLE_BLOCK_SLOTS) + (uint32_t)(e - base);
    }
    return HANDLE_MAX_SLOTS; /* not found: BitmapSet/Clr below are no-ops on OOB */
}

/* Mark a slot allocated. Call once, after every error check has passed and
 * the entry's type/union are filled in. */
static inline void HandleEntryClaim(HandleTable *t, HandleEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapSet(t->slot_bitmap, i);
}

static inline void HandleEntryFree(HandleTable *t, HandleEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapClr(t->slot_bitmap, i);
    memset(e, 0, sizeof(*e));
}

#endif
