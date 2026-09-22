#ifndef _ZUZU_HANDLE_H
#define _ZUZU_HANDLE_H

#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/types.h>

#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"

#include "kernel/dev/devcap.h"
#include "kernel/task/task.h"
#include "kernel/ipc/ntfn.h"
#include "kernel/ipc/port.h"

#define HANDLE_INDEX_BITS 10u                              /* log2(HANDLE_MAX_SLOTS) */
#define HANDLE_INDEX_MASK ((1u << HANDLE_INDEX_BITS) - 1u) /* 0x3FF */

#define HANDLE_PACK(index, gen)                                                                    \
    (((uint32_t)(gen) << HANDLE_INDEX_BITS) | ((uint32_t)(index) & HANDLE_INDEX_MASK))
#define HANDLE_INDEX(h) ((uint32_t)(h) & HANDLE_INDEX_MASK)
#define HANDLE_GEN(h) ((uint32_t)(h) >> HANDLE_INDEX_BITS)

#define GRANT_REGRANTABLE (1u << 0)

typedef struct SpaceObjectStruct SpaceObject;

typedef enum
{
    HANDLE_FREE,
    HANDLE_PORT,
    HANDLE_MEM,
    HANDLE_EVENT,
    HANDLE_TASK,
    HANDLE_SPACE
} HandleType;

typedef enum
{
    MEM_KIND_NONE,
    MEM_KIND_DEV,
    MEM_KIND_SHM,
} MemKind;

typedef struct
{
    HandleType type;    /* HANDLE_* */
    bool grantable;     /* Will grant() work on this handle? */
    VirtAddr mapped_va; /* For shm and device: destroy() checks before freeing */
    MemKind mem_kind;   /* Only meaningful when type == HANDLE_MEM. */
    union
    {
        PortObject *port;
        DeviceObject *dev;
        ShmObject *shm;
        EventObject *event; 
        TaskObject *task;
        SpaceObject *space;
    };
    Marker marker; /* Added in zuzu 1.1: Same handle can be stamped with a marker to demux
                      clients sharing a port */
    uint32_t generation;
} HandleTableEntry;

#define HANDLE_BLOCK_SLOTS 64U
#define HANDLE_MAX_BLOCKS 16U
#define HANDLE_MAX_SLOTS (HANDLE_BLOCK_SLOTS * HANDLE_MAX_BLOCKS) /* 1024 */

typedef HandleTableEntry HandleBlock[HANDLE_BLOCK_SLOTS];

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
static __always_inline HandleTableEntry *HandleTableGet(HandleTable *t, uint32_t i)
{
    if (unlikely(i >= HANDLE_MAX_SLOTS))
        return NULL;
    HandleBlock *blk = t->blocks[i / HANDLE_BLOCK_SLOTS];
    if (unlikely(!blk))
        return NULL;
    return &(*blk)[i % HANDLE_BLOCK_SLOTS];
}

/* Returns a free slot index, allocating its leaf block on first use. */
static inline Handle HandleTableFindFree(HandleTable *t)
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
    return (Handle)slot;
}

static inline HandleTableEntry *HandleTableLookup(HandleTable *t, Handle h)
{
    HandleTableEntry *e = HandleTableGet(t, HANDLE_INDEX(h));
    if (!e || e->type == HANDLE_FREE || e->generation != HANDLE_GEN(h))
        return NULL;
    return e;
}

static inline HandleTableEntry *HandleTableGetOrAlloc(HandleTable *t, uint32_t i)
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
static inline uint32_t HandleEntryIndex(const HandleTable *t, const HandleTableEntry *e)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++)
    {
        const HandleTableEntry *base = t->blocks[b] ? (*t->blocks[b]) : NULL;
        if (base && e >= base && e < base + HANDLE_BLOCK_SLOTS)
            return (b * HANDLE_BLOCK_SLOTS) + (uint32_t)(e - base);
    }
    return HANDLE_MAX_SLOTS;
}

/* Mark a slot allocated. Call once, after every error check has passed and
 * the entry's type/union are filled in. */
static inline void HandleEntryClaim(HandleTable *t, HandleTableEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapSet(t->slot_bitmap, i);
}

static inline void HandleEntryFree(HandleTable *t, HandleTableEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapClr(t->slot_bitmap, i);
    uint32_t next_gen = e->generation + 1;
    memset(e, 0, sizeof(*e));
    e->generation = next_gen;
}

#endif /* _ZUZU_HANDLE_H */
