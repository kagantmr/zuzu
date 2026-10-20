/**
 * @file handle.h
 * @brief A handle is simply an opaque integer index to the kernel-kept object table. Handle tables
 * are made up of directories and actual entries and are allocated lazily.
 */

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

/** @brief Number of low bits of a handle value that hold the slot index. */
#define HANDLE_INDEX_BITS 10u
/** @brief Mask that extracts the slot index from a handle value. */
#define HANDLE_INDEX_MASK ((1u << HANDLE_INDEX_BITS) - 1u)

/**
 * @brief Build a handle value from a slot index and the slot's generation.
 *
 * @param index The slot index.
 * @param gen The slot's current generation.
 */
#define HANDLE_PACK(index, gen)                                                                    \
    (((uint32_t)(gen) << HANDLE_INDEX_BITS) | ((uint32_t)(index) & HANDLE_INDEX_MASK))
/** @brief Get the slot index out of a handle value. */
#define HANDLE_INDEX(h) ((uint32_t)(h) & HANDLE_INDEX_MASK)
/** @brief Get the generation out of a handle value. */
#define HANDLE_GEN(h) ((uint32_t)(h) >> HANDLE_INDEX_BITS)
/** @brief Number of bits left for the generation, keeping handle values non-negative. */
#define HANDLE_GEN_BITS (31u - HANDLE_INDEX_BITS)
/** @brief Mask the generation wraps with when a slot is freed. */
#define HANDLE_GEN_MASK ((1u << HANDLE_GEN_BITS) - 1u)

/** @brief Forward declaration of a space. */
typedef struct SpaceObjectStruct SpaceObject;

/** @brief What kind of object a handle table entry refers to. */
typedef enum {
    HANDLE_FREE,      /**< The slot is unused. */
    HANDLE_PORT,      /**< A port. */
    HANDLE_MEM,       /**< A memory object, shared or device. */
    HANDLE_EVENT,     /**< An event. */
    HANDLE_TASK,      /**< A task. */
    HANDLE_SPACE,     /**< A space. */
    HANDLE_TYPE_COUNT /**< The number of types; not a valid type. */
} HandleType;

/**
 * @brief One slot of a handle table: a counted reference to an object plus what the holder may do
 * with it.
 */
typedef struct HandleTableEntryStruct {
    HandleType type;    /**< The kind of object, which selects the member of the union in use. */
    HandlePerms perms;  /**< What the holder may do with the object. */
    VirtAddr mapped_va; /**< For memory objects, where this handle has it mapped, or 0. */
    union {
        PortObject *port;   /**< The object when @c type is HANDLE_PORT. */
        MemObject *mem;     /**< The object when @c type is HANDLE_MEM. */
        EventObject *event; /**< The object when @c type is HANDLE_EVENT. */
        TaskObject *task;   /**< The object when @c type is HANDLE_TASK. */
        SpaceObject *space; /**< The object when @c type is HANDLE_SPACE. */
    };
    Marker marker;       /**< Badge a server sees when this handle is used to Call() it. */
    uint32_t generation; /**< Bumped each time the slot is freed, so stale handles stop matching. */
} HandleTableEntry;

/** @brief Number of entries in one block of a handle table. */
#define HANDLE_BLOCK_SLOTS 64U
/** @brief Number of blocks a handle table can hold. */
#define HANDLE_MAX_BLOCKS 16U
/** @brief Number of handles one space can have. */
#define HANDLE_MAX_SLOTS (HANDLE_BLOCK_SLOTS * HANDLE_MAX_BLOCKS)

/** @brief A block of entries, allocated as a unit. */
typedef HandleTableEntry HandleBlock[HANDLE_BLOCK_SLOTS];

/**
 * @brief A space's handle table: blocks of entries, allocated the first time a slot in them is
 * needed.
 */
typedef struct {
    HandleBlock *blocks[HANDLE_MAX_BLOCKS]; /**< The blocks; NULL for those not allocated yet. */
    uint32_t
        slot_bitmap[BITMAP_WORDS(HANDLE_MAX_SLOTS)]; /**< One bit per slot, set while in use. */
} HandleTable;

/**
 * @brief Set up an empty handle table.
 *
 * @param[in] t The table to initialize.
 */
static inline void HandleTableInit(HandleTable *t) { memset(t, 0, sizeof(*t)); }

/**
 * @brief Free every block of a table. The entries are not released first; see HandleRelease().
 *
 * @param[in] t The table to tear down.
 */
static inline void HandleTableDestroy(HandleTable *t)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++) {
        KFree(t->blocks[b]);
        t->blocks[b] = NULL;
    }
    BitmapZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
}

/**
 * @brief Get the entry at a slot index, whether or not it is in use.
 *
 * @param[in] t The table.
 * @param[in] i The slot index, not a packed handle value.
 * @return The entry, or NULL if @p i is out of range or its block is not allocated.
 */
static __always_inline HandleTableEntry *HandleTableGet(HandleTable *t, Handle i)
{
    if (unlikely((uint32_t)i >= HANDLE_MAX_SLOTS))
        return NULL;
    HandleBlock *blk = t->blocks[(uint32_t)i / HANDLE_BLOCK_SLOTS];
    if (unlikely(!blk))
        return NULL;
    return &(*blk)[(uint32_t)i % HANDLE_BLOCK_SLOTS];
}

/**
 * @brief Allocate the block that holds a slot if it does not exist yet.
 *
 * @param[in] t The table.
 * @param[in] i The slot index.
 * @return true if the block exists afterwards, false if memory ran out.
 */
static inline bool HandleBlockEnsure(HandleTable *t, uint32_t i)
{
    uint32_t b = i / HANDLE_BLOCK_SLOTS;
    if (!t->blocks[b])
        t->blocks[b] = KZAlloc(sizeof(HandleBlock));
    return t->blocks[b] != NULL;
}

/**
 * @brief Find an unused slot, allocating its block if needed. The slot is not claimed.
 *
 * @param[in] t The table.
 * @return The slot index, or -1 if the table is full or memory ran out.
 */
static inline Handle HandleTableFindFree(HandleTable *t)
{
    int slot = BitmapFindFirstZero(t->slot_bitmap, HANDLE_MAX_SLOTS);
    if (slot < 0 || !HandleBlockEnsure(t, (uint32_t)slot))
        return -1;
    return (Handle)slot;
}

/**
 * @brief Resolve a handle value to its entry, checking that it is in use and not stale.
 *
 * @param[in] t The table.
 * @param[in] h A packed handle value as user space holds it.
 * @return The entry, or NULL if the slot is free or the generation does not match.
 */
static inline HandleTableEntry *HandleTableLookup(HandleTable *t, Handle h)
{
    HandleTableEntry *e = HandleTableGet(t, HANDLE_INDEX(h));
    if (!e || e->type == HANDLE_FREE || e->generation != HANDLE_GEN(h))
        return NULL;
    return e;
}

/**
 * @brief Get the entry at a slot index, allocating its block if needed.
 *
 * @param[in] t The table.
 * @param[in] i The slot index.
 * @return The entry, or NULL if @p i is out of range or memory ran out.
 */
static inline HandleTableEntry *HandleTableGetOrAlloc(HandleTable *t, Handle i)
{
    if ((uint32_t)i >= HANDLE_MAX_SLOTS || !HandleBlockEnsure(t, (uint32_t)i))
        return NULL;
    return &(*t->blocks[(uint32_t)i / HANDLE_BLOCK_SLOTS])[(uint32_t)i % HANDLE_BLOCK_SLOTS];
}

/**
 * @brief Find the slot index of an entry by its address.
 *
 * @param[in] t The table the entry belongs to.
 * @param[in] e The entry.
 * @return The slot index, or HANDLE_MAX_SLOTS if @p e is not in the table.
 */
static inline uint32_t HandleEntryIndex(const HandleTable *t, const HandleTableEntry *e)
{
    for (uint32_t b = 0; b < HANDLE_MAX_BLOCKS; b++) {
        const HandleTableEntry *base = t->blocks[b] ? (*t->blocks[b]) : NULL;
        if (base && e >= base && e < base + HANDLE_BLOCK_SLOTS)
            return (b * HANDLE_BLOCK_SLOTS) + (uint32_t)(e - base);
    }
    return HANDLE_MAX_SLOTS;
}

/**
 * @brief Mark an entry's slot as in use.
 *
 * @param[in] t The table the entry belongs to.
 * @param[in] e The entry.
 */
static inline void HandleEntryClaim(HandleTable *t, HandleTableEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapSet(t->slot_bitmap, i);
}

/**
 * @brief Clear an entry, mark its slot free, and advance its generation (wrapping).
 *
 * The object reference is not dropped; see HandleRelease().
 *
 * @param[in] t The table the entry belongs to.
 * @param[in] e The entry.
 */
static inline void HandleEntryFree(HandleTable *t, HandleTableEntry *e)
{
    uint32_t i = HandleEntryIndex(t, e);
    if (i < HANDLE_MAX_SLOTS)
        BitmapClr(t->slot_bitmap, i);
    uint32_t next_gen = (e->generation + 1) & HANDLE_GEN_MASK;
    memset(e, 0, sizeof(*e));
    e->generation = next_gen;
}

/**
 * @brief Give another table a new handle to the object an entry refers to.
 *
 * Takes a reference on the object. The new handle gets @p perms and @p marker exactly as given,
 * so the caller narrows them first.
 *
 * @param[in] dst_table The table to add the handle to.
 * @param[in] src The entry to copy.
 * @param[in] perms The permissions of the new handle.
 * @param[in] marker The marker of the new handle.
 * @param[out] out Receives the new handle value.
 * @return ZUZU_OK, ERR_BADTYPE if @p src is not a live entry, or ERR_NOMEM if @p dst_table is
 * full.
 */
Err HandleCopyInto(HandleTable *dst_table, HandleTableEntry *src, HandlePerms perms, Marker marker,
                   Handle *out);

/**
 * @brief Drop the reference an entry holds and free its slot.
 *
 * Releasing the last handle a space has to a device also releases the interrupts the space owned
 * for it.
 *
 * @param[in] sp The space that owns the table.
 * @param[in] entry The entry to release.
 */
void HandleRelease(SpaceObject *sp, HandleTableEntry *entry);

#endif /* KERNEL_SPACE_HANDLE_H */
