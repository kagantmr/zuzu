#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

#define align_down(addr, align) ((addr) & ~((align) - 1))
#define align_up(addr, align)   (((addr) + (align) - 1) & ~((align) - 1))

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
 * @brief Scan a memory area for the first occurrence of a specified byte.
 * @param s Pointer to the memory area to scan.
 * @param c Byte value to search for (interpreted as an unsigned char).
 * @param n Number of bytes to scan.
 */
void *memchr(const void *s, int c, size_t n);

#endif /* MEM_H */
