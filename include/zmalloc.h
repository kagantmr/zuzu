#ifndef ZMALLOC_H
#define ZMALLOC_H

#ifndef __KERNEL__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *zmalloc(size_t size);
void *zcalloc(size_t count, size_t size);
void *zrealloc(void *ptr, size_t size);
void zfree(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* ZMALLOC_H */
