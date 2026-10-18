#ifndef KERNEL_SPACE_HANDLE_H
#define KERNEL_SPACE_HANDLE_H

#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm/vmm.h"

#include "kernel/ipc/event.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/mem_object.h"
#include "kernel/task/task.h"

#define HANDLE_INDEX_BITS 10u
#define HANDLE_INDEX_MASK ((1u << HANDLE_INDEX_BITS) - 1u)

#define HANDLE_PACK(index, gen)                                                                    \
    (((uint32_t)(gen) << HANDLE_INDEX_BITS) | ((uint32_t)(index) & HANDLE_INDEX_MASK))
#define HANDLE_INDEX(h) ((uint32_t)(h) & HANDLE_INDEX_MASK)
#define HANDLE_GEN(h) ((uint32_t)(h) >> HANDLE_INDEX_BITS)
#define HANDLE_GEN_BITS (31u - HANDLE_INDEX_BITS)
#define HANDLE_GEN_MASK ((1u << HANDLE_GEN_BITS) - 1u)

typedef struct SpaceObjectStruct SpaceObject;

typedef enum {
    HANDLE_FREE,
    HANDLE_PORT,
    HANDLE_MEM,
    HANDLE_EVENT,
    HANDLE_TASK,
    HANDLE_SPACE,
    HANDLE_TYPE_COUNT
} HandleType;

typedef struct HandleTableEntryStruct {
    HandleType type;
    HandlePerms perms;
    VirtAddr mapped_va;
    union {
        PortObject *port;
        MemObject *mem;
        EventObject *event;
        TaskObject *task;
        SpaceObject *space;
    };
    Marker marker; 
    uint32_t generation;
} HandleTableEntry;

#define HANDLE_BLOCK_SLOTS 64U
#define HANDLE_MAX_BLOCKS 16U
#define HANDLE_MAX_SLOTS (HANDLE_BLOCK_SLOTS * HANDLE_MAX_BLOCKS)

typedef HandleTableEntry HandleBlock[HANDLE_BLOCK_SLOTS];

typedef struct {
    HandleBlock *blocks[HANDLE_MAX_BLOCKS];
    uint32_t slot_bitmap[BITMAP_WORDS(HANDLE_MAX_SLOTS)];
} HandleTable;

static inline void HandleTableInit(HandleTable *t) { memset(t, 0, sizeof(*t)); }

static inline void HandleTableDestroy(HandleTable *t)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++) {
        KFree(t->blocks[b]);
        t->blocks[b] = NULL;
    }
    BitmapZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
}

static __always_inline HandleTableEntry *HandleTableGet(HandleTable *t, Handle i)
{
    if (unlikely((uint32_t)i >= HANDLE_MAX_SLOTS))
        return NULL;
    HandleBlock *blk = t->blocks[(uint32_t)i / HANDLE_BLOCK_SLOTS];
    if (unlikely(!blk))
        return NULL;
    return &(*blk)[(uint32_t)i % HANDLE_BLOCK_SLOTS];
}

static inline bool HandleBlockEnsure(HandleTable *t, uint32_t i)
{
    uint32_t b = i / HANDLE_BLOCK_SLOTS;
    if (!t->blocks[b])
        t->blocks[b] = KZAlloc(sizeof(HandleBlock));
    return t->blocks[b] != NULL;
}

static inline Handle HandleTableFindFree(HandleTable *t)
{
    int slot = BitmapFindFirstZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
    if (slot < 0 || !HandleBlockEnsure(t, (uint32_t)slot))
        return -1;
    return (Handle)slot;
}

static inline HandleTableEntry *HandleTableLookup(HandleTable *t, Handle h)
{
    HandleTableEntry *e = HandleTableGet(t, HANDLE_INDEX(h));
    if (!e || e->type == HANDLE_FREE || e->generation != HANDLE_GEN(h))
        return NULL;
    return e;
}

static inline HandleTableEntry *HandleTableGetOrAlloc(HandleTable *t, Handle i)
{
    if ((uint32_t)i >= HANDLE_MAX_SLOTS || !HandleBlockEnsure(t, (uint32_t)i))
        return NULL;
    return &(*t->blocks[(uint32_t)i / HANDLE_BLOCK_SLOTS])[(uint32_t)i % HANDLE_BLOCK_SLOTS];
}

static inline uint32_t HandleEntryIndex(const HandleTable *t, const HandleTableEntry *e)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++) {
        const HandleTableEntry *base = t->blocks[b] ? (*t->blocks[b]) : NULL;
        if (base && e >= base && e < base + HANDLE_BLOCK_SLOTS)
            return (b * HANDLE_BLOCK_SLOTS) + (uint32_t)(e - base);
    }
    return HANDLE_MAX_SLOTS;
}

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
    uint32_t next_gen = (e->generation + 1) & HANDLE_GEN_MASK;
    memset(e, 0, sizeof(*e));
    e->generation = next_gen;
}

Err HandleCopyInto(HandleTable *dst_table, HandleTableEntry *src, HandlePerms perms, Marker marker,
                   Handle *out);

void HandleRelease(SpaceObject *sp, HandleTableEntry *entry);

#endif /* KERNEL_SPACE_HANDLE_H */
