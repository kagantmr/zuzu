#ifndef VECTOR_H
#define VECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/mm/alloc.h"
#include <mem.h>

#define VEC_INITIAL_CAP 16

#define DEFINE_VEC(name, type)                                      \
                                                                    \
    typedef struct                                                  \
    {                                                               \
        type *data;                                                 \
        uint32_t len;                                               \
        uint32_t cap;                                               \
    } name##_vec_t;                                                 \
                                                                    \
    static inline bool name##_vec_init(name##_vec_t *v)             \
    {                                                               \
        v->data = kmalloc(VEC_INITIAL_CAP * sizeof(type));          \
        if (!v->data)                                               \
            return false;                                           \
        v->len = 0;                                                 \
        v->cap = VEC_INITIAL_CAP;                                   \
        memset(v->data, 0, VEC_INITIAL_CAP * sizeof(type));         \
        return true;                                                \
    }                                                               \
                                                                    \
    static inline type *name##_vec_get(name##_vec_t *v, uint32_t i) \
    {                                                               \
        if (i >= v->cap)                                            \
            return NULL;                                            \
        return &v->data[i];                                         \
    }                                                               \
                                                                    \
    static inline int name##_vec_grow(name##_vec_t *v)              \
    {                                                               \
        uint32_t new_cap = v->cap * 2;                              \
        type *new_data = kmalloc(new_cap * sizeof(type));           \
        if (!new_data)                                              \
            return -1;                                              \
        memset(new_data, 0, new_cap * sizeof(type));                \
        memmove(new_data, v->data, v->cap * sizeof(type));          \
        kfree(v->data);                                             \
        v->data = new_data;                                         \
        v->cap = new_cap;                                           \
        return 0;                                                   \
    }                                                               \
                                                                    \
    static inline void name##_vec_destroy(name##_vec_t *v)          \
    {                                                               \
        kfree(v->data);                                             \
        v->data = NULL;                                             \
        v->len = 0;                                                 \
        v->cap = 0;                                                 \
    }

#endif /* VECTOR_H */
