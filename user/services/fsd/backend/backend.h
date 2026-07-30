#ifndef FSD_BACKEND_H
#define FSD_BACKEND_H

#include <zuzu/types.h>
#include <zuzu/protocols/fsd.h>

#define MAX_BACKEND_FILE_SIZE 2048

typedef struct {
    const char *name;
    Err (*open)(void *ctx, void *file, const char *path, uint32_t mode);
    Err (*close)(void *ctx, void *file);
    Err (*read)(void *ctx, void *file, void *buf, uint32_t count, uint32_t *got);
    Err (*write)(void *ctx, void *file, const void *buf, uint32_t count, uint32_t *put);
    Err (*seek)(void *ctx, void *file, int64_t off, uint32_t whence, int64_t *newpos);
    Err (*stat)(void *ctx, const char *path, FsdStat *out);
    Err (*readdir)(void *ctx, const char *path, uint32_t start,
                    FsdDirEntry *out, uint32_t max, uint32_t *count);
    Err (*unlink)(void *ctx, const char *path);
    Err (*rename)(void *ctx, const char *from, const char *to);
    Err (*mount)(void **ctx_out);
    Err (*unmount)(void *ctx);
    size_t file_size;   /* sizeof(FIL) so fsd can allocate the pool */
} fs_backend_t;

extern const fs_backend_t fat_backend;

#endif