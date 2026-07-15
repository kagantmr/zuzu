#ifndef MALLOC_H
#define MALLOC_H

#ifndef __KERNEL__

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Allocates a block of memory of the specified size.
     *
     * @param size The size of the memory block to allocate, in bytes.
     * @return void* Pointer to the allocated memory block, or NULL if the allocation fails.
     */
    void *malloc(size_t size);

    /**
     * @brief Allocates a block of memory for an array of elements, initializes all bytes to zero.
     *
     * @param count The number of elements to allocate.
     * @param size The size of each element, in bytes.
     * @return void* Pointer to the allocated memory block, or NULL if the allocation fails.
     */
    void *calloc(size_t count, size_t size);

    /**
     * @brief Changes the size of the memory block pointed to by ptr to size bytes.
     *
     * @param ptr Pointer to the memory block to resize. If NULL, behaves like malloc.
     * @param size The new size of the memory block, in bytes.
     * @return void* Pointer to the reallocated memory block, or NULL if the reallocation fails. If the new size is larger, the contents of the old memory block are preserved up to the minimum of the old and new sizes.
     */
    void *realloc(void *ptr, size_t size);

    /**
     * @brief Frees the memory block pointed to by ptr, which must have been returned by a previous call to malloc, calloc, or realloc. If ptr is NULL, no operation is performed.
     */
    void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* MALLOC_H */
