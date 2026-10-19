/**
 * @file
 * @brief Program break for the heap allocator.
 */

#ifndef SBRK_H
#define SBRK_H

#include <types.h>

/** @brief The heap's reservation and current break. */
typedef struct arena {
    VirtAddr base;   /**< Start of the reservation. */
    VirtAddr brk;    /**< Current break. */
    VirtAddr mapped; /**< End of the reservation. */
} Arena;

/**
 * @brief Moves the program break by @p incr bytes.
 *
 * The first call reserves 32 MiB of anonymous memory, filled on demand; the
 * break moves within that reservation only.
 *
 * @param[in] incr Bytes to grow by, or a negative number to shrink by.
 * @return The previous break, or (void *)-1 if the reservation cannot be made or the break would
 *         leave it.
 */
void *sbrk(intptr_t incr);

#endif