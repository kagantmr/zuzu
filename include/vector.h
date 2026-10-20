/**
 * @file
 * @brief Type-safe growable arrays, generated per element type by DEFINE_VEC().
 */

#ifndef VECTOR_H
#define VECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __ZUZU__
#include "kernel/mm/alloc.h"
/** @brief Allocates zeroed memory: KZAlloc() in the kernel, malloc() in userspace. */
#define VEC_ALLOC(sz) KZAlloc(sz)
/** @brief Frees memory from VEC_ALLOC(). */
#define VEC_FREE(ptr) KFree(ptr)
#else
#include <malloc.h>
/** @brief Allocates memory with malloc(). */
#define VEC_ALLOC(sz) malloc(sz)
/** @brief Frees memory from VEC_ALLOC(). */
#define VEC_FREE(ptr) free(ptr)
#endif

/** @brief Allocates an array of @p count elements of @p type. */
#define VEC_ALLOC_T(type, count) ((type *)VEC_ALLOC((count) * sizeof(type)))

/** @brief Capacity of a new vector. */
#define VEC_INITIAL_CAP 16

/**
 * @brief Defines a vector type for elements of @p type, and its functions.
 *
 * Generates:
 * - `name##_vec_t`: { type *data; uint32_t len; uint32_t cap; }
 * - `bool name##_vec_init(v)`: allocates VEC_INITIAL_CAP zeroed slots; false if out of memory.
 * - `type *name##_vec_get(v, i)`: slot @c i, or NULL if @c i >= cap. Bounds are checked against
 *   the capacity, not the length.
 * - `const type *name##_vec_get_const(v, i)`: the same for a const vector.
 * - `int name##_vec_grow(v)`: doubles the capacity, zeroing the new slots; -1 if out of memory.
 * - `void name##_vec_destroy(v)`: frees the storage.
 * - `bool name##_vec_push(v, item)`: appends a copy of *item, growing as needed; false if out of
 *   memory.
 */
#define DEFINE_VEC(name, type)                                                                     \
                                                                                                   \
    typedef struct {                                                                               \
        type *data;                                                                                \
        uint32_t len;                                                                              \
        uint32_t cap;                                                                              \
    } name##_vec_t;                                                                                \
                                                                                                   \
    static inline bool name##_vec_init(name##_vec_t *v)                                            \
    {                                                                                              \
        v->data = VEC_ALLOC_T(type, VEC_INITIAL_CAP);                                              \
        if (!v->data)                                                                              \
            return false;                                                                          \
        v->len = 0;                                                                                \
        v->cap = VEC_INITIAL_CAP;                                                                  \
        memset(v->data, 0, VEC_INITIAL_CAP * sizeof(type));                                        \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* Bounds check + array index, called on every handle-table touch                              \
     * (every send/recv/call/reply/notify/irq/memmap syscall) -- a leaf                            \
     * in the always_inline sense: no loop, no calls, nothing to gain                              \
     * by keeping it out-of-line. */                                                               \
    static __always_inline type *name##_vec_get(name##_vec_t *v, uint32_t i)                       \
    {                                                                                              \
        if (unlikely(i >= v->cap))                                                                 \
            return NULL;                                                                           \
        return &v->data[i];                                                                        \
    }                                                                                              \
                                                                                                   \
    /* Read-only counterpart for callers that only hold a const owner                              \
     * (e.g. a const AddressSpace *) -- avoids casting the const away                              \
     * just to call name##_vec_get. */                                                             \
    static __always_inline const type *name##_vec_get_const(const name##_vec_t *v, uint32_t i)     \
    {                                                                                              \
        if (unlikely(i >= v->cap))                                                                 \
            return NULL;                                                                           \
        return &v->data[i];                                                                        \
    }                                                                                              \
                                                                                                   \
    static inline int name##_vec_grow(name##_vec_t *v)                                             \
    {                                                                                              \
        uint32_t new_cap = v->cap * 2;                                                             \
        type *new_data = VEC_ALLOC_T(type, new_cap);                                               \
        if (!new_data)                                                                             \
            return -1;                                                                             \
        memset(new_data, 0, new_cap * sizeof(type));                                               \
        memmove(new_data, v->data, v->cap * sizeof(type));                                         \
        VEC_FREE(v->data);                                                                         \
        v->data = new_data;                                                                        \
        v->cap = new_cap;                                                                          \
        return 0;                                                                                  \
    }                                                                                              \
                                                                                                   \
    static inline void name##_vec_destroy(name##_vec_t *v)                                         \
    {                                                                                              \
        VEC_FREE(v->data);                                                                         \
        v->data = NULL;                                                                            \
        v->len = 0;                                                                                \
        v->cap = 0;                                                                                \
    }                                                                                              \
                                                                                                   \
    static inline bool name##_vec_push(name##_vec_t *v, const type *item)                          \
    {                                                                                              \
        if (v->len >= v->cap) {                                                                    \
            if (name##_vec_grow(v) < 0)                                                            \
                return false;                                                                      \
        }                                                                                          \
        v->data[v->len++] = *item;                                                                 \
        return true;                                                                               \
    }

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_H */
