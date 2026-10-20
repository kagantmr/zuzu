/**
 * @file
 * @brief Small cache of loaded program images, keyed by path.
 *
 * Lets a shell spawn the same program again without re-reading it from fsd.
 * An entry is valid while the file's size and modification time match.
 */

#ifndef ZUZU_IMGCACHE_H
#define ZUZU_IMGCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

/** @brief Number of images kept; the least recently used is evicted beyond that. */
#define IMGCACHE_SLOTS 8
/** @brief Longest path that can be cached, including the NUL. */
#define IMGCACHE_PATH_MAX 128

/** @brief One cached image. */
typedef struct {
    char path[IMGCACHE_PATH_MAX]; /**< Path the image was read from. */
    uint32_t size;                /**< File size when it was cached. */
    uint32_t mtime;               /**< File modification time when it was cached. */
    uint32_t last_used;           /**< Value of the cache clock at the last hit; for LRU eviction. */
    Handle obj;                   /**< Memory object holding the image, or -1 if the slot is free. */
} ImgCacheEntry;

/** @brief The cache. A zero-initialised ImgCache is ready to use. */
typedef struct {
    ImgCacheEntry slots[IMGCACHE_SLOTS]; /**< Cached images. */
    uint32_t clock;                      /**< Counter bumped on every hit or insert. */
    bool ready;                          /**< Set once the slots have been initialised. */
} ImgCache;

/**
 * @brief Looks up the image cached for @p path.
 *
 * An entry whose size or mtime no longer matches is dropped and counts as a miss.
 *
 * @param[in,out] c     The cache.
 * @param[in]     path  Path of the program.
 * @param[in]     size  Current file size.
 * @param[in]     mtime Current file modification time.
 * @return The image object, still owned by the cache, or -1 on a miss.
 */
Handle ImgCacheGet(ImgCache *c, const char *path, uint32_t size, uint32_t mtime);

/**
 * @brief Caches @p obj under @p path and takes ownership of it.
 *
 * Replaces any previous entry for @p path; when every slot is taken, closes
 * the least recently used entry.
 *
 * @param[in,out] c     The cache.
 * @param[in]     path  Path of the program.
 * @param[in]     size  File size.
 * @param[in]     mtime File modification time.
 * @param[in]     obj   Memory object holding the image.
 * @return true if cached; false if @p path is too long, and the caller still owns @p obj.
 */
bool ImgCachePut(ImgCache *c, const char *path, uint32_t size, uint32_t mtime, Handle obj);

/**
 * @brief Closes and forgets the entry for @p path, if any.
 *
 * @param[in,out] c    The cache.
 * @param[in]     path Path of the program.
 */
void ImgCacheDrop(ImgCache *c, const char *path);

#ifdef __cplusplus
}
#endif

#endif
