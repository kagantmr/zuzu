#ifndef ARCH_CACHE_H
#define ARCH_CACHE_H

#include <stddef.h>
#include <stdint.h>

void ArchCacheCleanDcacheRangePou(uintptr_t start, size_t size);

void ArchCacheInvalidateIcacheAll(void);

void ArchCacheCleanInvalidateDcacheRange(uintptr_t start, size_t size);

void ArchCacheInvalidateDcacheRange(uintptr_t start, size_t size);

#endif // ARCH_CACHE_H
