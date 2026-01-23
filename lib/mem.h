#ifndef MEM_H
#define MEM_H
#include "stddef.h"
#include "stdint.h"
#include "stdbool.h"



/**
 * @brief Byte-swap a 32-bit unsigned integer.
 * 
 * @param x The 32-bit unsigned integer to byte-swap.
 * @return The byte-swapped 32-bit unsigned integer.
 */
uint32_t bswap32(uint32_t x);

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
 * @brief Compare two memory areas byte by byte.
 * @param str1 Pointer to the first memory area.
 * @param str2 Pointer to the second memory area.
 * @param count Number of bytes to compare.
 * @return An integer less than, equal to, or greater than zero if str1 is found, respectively, to be less than, to match, or be greater than str2.
 */
int memcmp(const void *str1, const void *str2, size_t count);

/** 
 * @brief Align an address downwards to the nearest multiple of alignment.
 * 
 * @param addr The address to align.
 * @param alignment The alignment boundary (must be a power of two).
 * @return The aligned address.
 * 
*/
uintptr_t align_down(uintptr_t addr, size_t alignment);

/** 
 * @brief Align an address upwards to the nearest multiple of alignment.
 * 
 * @param addr The address to align.
 * @param alignment The alignment boundary (must be a power of two).
 * @return The aligned address.
 * 
*/
uintptr_t align_up(uintptr_t addr, size_t alignment);

#endif /* MEM_H */
