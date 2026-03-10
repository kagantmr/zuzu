#ifndef ZMALLOC_H
#define ZMALLOC_H

#include <stddef.h>

void *zmalloc(size_t size);

void *zcalloc(size_t count, size_t size);

void *zrealloc(void *ptr, size_t size);

void zfree(void *ptr);

#endif