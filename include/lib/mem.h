#ifndef MEM_H
#define MEM_H
#include "stddef.h"
#include "stdint.h"

/**
 * @brief Copy n bytes from source to destination.
 * 
 * @param dest Pointer to the destination memory area.
 * @param src  Pointer to the source memory area.
 * @param n    Number of bytes to copy.
 * @return Pointer to the destination memory area.
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * @brief Set n bytes of memory to a specified value.
 * 
 * @param ptr Pointer to the memory area to set.
 * @param x   Value to set (interpreted as an unsigned char).
 * @param n   Number of bytes to set.
 * @return Pointer to the memory area.
 */
void *memset(void *ptr, char x, size_t n);

/**
 * @brief Move n bytes from source to destination, handling overlapping regions.
 * 
 * @param dest Pointer to the destination memory area.
 * @param src  Pointer to the source memory area.
 * @param n    Number of bytes to move.
 * @return Pointer to the destination memory area.
 */
void *memmove(void *dest, const void *src, size_t n);

/** 
 * @brief Align an address upwards to the nearest multiple of alignment.
 * 
 * @param addr The address to align.
 * @param alignment The alignment boundary (must be a power of two).
 * @return The aligned address.
 * 
*/
uintptr_t align_up(uintptr_t addr, size_t alignment);

#endif