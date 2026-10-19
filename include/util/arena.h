/**
 * @file
 * @brief Bump allocator over a caller-provided buffer.
 *
 * Allocations only move a cursor forward; nothing is freed individually.
 * ArenaReset() releases everything at once.
 */

#ifndef ZUZU_ARENA_H
#define ZUZU_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** @brief An arena: a buffer and how much of it is used. */
typedef struct {
    uint8_t *base; /**< Start of the buffer. */
    size_t size;   /**< Buffer size in bytes. */
    size_t offset; /**< Bytes handed out so far. */
} Arena;

/**
 * @brief Sets up an arena over @p buf. The arena does not own the buffer.
 *
 * @param[out] a    Arena to initialise.
 * @param[in]  buf  Backing buffer.
 * @param[in]  size Size of @p buf in bytes.
 */
void ArenaInit(Arena *a, void *buf, size_t size);

/**
 * @brief Takes @p size bytes from the arena, with no alignment.
 *
 * @param[in,out] a    Arena to allocate from.
 * @param[in]     size Bytes wanted.
 * @return The memory, or NULL if fewer than @p size bytes are left.
 */
void *ArenaAlloc(Arena *a, size_t size);

/**
 * @brief Takes @p size bytes from the arena, aligned to @p align.
 *
 * Skips padding bytes as needed to reach the alignment.
 *
 * @param[in,out] a     Arena to allocate from.
 * @param[in]     size  Bytes wanted.
 * @param[in]     align Alignment in bytes; must be a power of two.
 * @return The memory, or NULL if padding plus @p size does not fit.
 */
void *ArenaAllocAligned(Arena *a, size_t size, size_t align);

/**
 * @brief Releases every allocation at once; the buffer is kept for reuse.
 *
 * @param[in,out] a Arena to reset.
 */
void ArenaReset(Arena *a);

/**
 * @brief Forgets the buffer and zeroes the arena. The buffer itself is not freed.
 *
 * @param[out] a Arena to clear.
 */
void ArenaDestroy(Arena *a);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ARENA_H