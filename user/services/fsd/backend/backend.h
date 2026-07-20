#ifndef FSD_BACKEND_H
#define FSD_BACKEND_H

#include <zuzu/types.h>
#include <zuzu/protocols/fsd_protocol.h>

#define MAX_BACKEND_FILE_SIZE 2048

typedef struct {
    const char *name;
    err_t (*open)(void *ctx, void *file, const char *path, uint32_t mode);
    err_t (*close)(void *ctx, void *file);
    err_t (*read)(void *ctx, void *file, void *buf, uint32_t count, uint32_t *got);
    err_t (*write)(void *ctx, void *file, const void *buf, uint32_t count, uint32_t *put);
    err_t (*seek)(void *ctx, void *file, int64_t off, uint32_t whence, int64_t *newpos);
    err_t (*stat)(void *ctx, const char *path, fsd_stat_t *out);
    err_t (*readdir)(void *ctx, const char *path, uint32_t start,
                    fsd_dirent_t *out, uint32_t max, uint32_t *count);
    err_t (*unlink)(void *ctx, const char *path);
    err_t (*rename)(void *ctx, const char *from, const char *to);
    err_t (*mount)(void **ctx_out);
    err_t (*unmount)(void *ctx);
    size_t file_size;   /* sizeof(FIL) so fsd can allocate the pool */
} fs_backend_t;

extern const fs_backend_t fat_backend;

#endif