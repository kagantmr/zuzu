/**
 * @file
 * @brief Userspace heap allocator.
 */

#ifndef MALLOC_H
#define MALLOC_H

#if !defined(__ZUZU__) || defined(__DOXYGEN__)

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocates a block of memory of the specified size.
 *
 * @param[in] size The size of the memory block to allocate, in bytes.
 * @return Pointer to the allocated memory block, or NULL if the allocation fails.
 */
void *malloc(size_t size);

/**
 * @brief Allocates a block of memory for an array of elements, initializes all bytes to zero.
 *
 * @param[in] count The number of elements to allocate.
 * @param[in] size  The size of each element, in bytes.
 * @return Pointer to the allocated memory block, or NULL if the allocation fails.
 */
void *calloc(size_t count, size_t size);

/**
 * @brief Changes the size of the memory block pointed to by ptr to size bytes.
 *
 * The contents are kept up to the smaller of the old and new sizes.
 *
 * @param[in] ptr  Pointer to the memory block to resize. If NULL, behaves like malloc.
 * @param[in] size The new size of the memory block, in bytes.
 * @return Pointer to the reallocated memory block, or NULL if the reallocation fails; @p ptr is
 *         then
 *         left unchanged.
 */
void *realloc(void *ptr, size_t size);

/**
 * @brief Frees a block from malloc(), calloc() or realloc().
 *
 * @param[in] ptr The block, or NULL to do nothing.
 */
void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* !__ZUZU__ || __DOXYGEN__ */

#endif /* MALLOC_H */
