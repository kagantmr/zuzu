#ifndef MALLOC_H
#define MALLOC_H

#ifndef __KERNEL__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* MALLOC_H */
