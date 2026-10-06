#ifndef ZUZU_IMGCACHE_H
#define ZUZU_IMGCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

#define IMGCACHE_SLOTS 8
#define IMGCACHE_PATH_MAX 128

typedef struct {
    char path[IMGCACHE_PATH_MAX];
    uint32_t size;
    uint32_t mtime;
    uint32_t last_used;
    Handle obj;
} ImgCacheEntry;

typedef struct {
    ImgCacheEntry slots[IMGCACHE_SLOTS];
    uint32_t clock;
    bool ready;
} ImgCache;

/**
 * @brief Returns the cached image object for path, or -1 on a miss. An entry whose size or
 * mtime no longer matches is dropped. The handle stays owned by the cache.
 */
Handle ImgCacheGet(ImgCache *c, const char *path, uint32_t size, uint32_t mtime);

/**
 * @brief Takes ownership of obj under path, closing any previous entry for that path or the
 * least-recently-used entry when the table is full.
 *
 * @return false if the path is too long to cache; the caller then still owns obj.
 */
bool ImgCachePut(ImgCache *c, const char *path, uint32_t size, uint32_t mtime, Handle obj);

/** @brief Closes and forgets the entry for path, if any. */
void ImgCacheDrop(ImgCache *c, const char *path);

#ifdef __cplusplus
}
#endif

#endif
