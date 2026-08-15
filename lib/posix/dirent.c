#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/fsd_client.h>

#define DIR_BATCH 16
#define DIR_PATH_MAX 256

struct DIR
{
    char path[DIR_PATH_MAX];
    uint32_t start;
    uint32_t count;
    uint32_t index;
    FsdDirEntry entries[DIR_BATCH];
    struct dirent cur;
};

static FsdConn g_dir_conn;

DIR *opendir(const char *path)
{
    if (!g_dir_conn.ready && FsdConnect(&g_dir_conn, FSD_SHM_DEFAULT) != ZUZU_OK) {
        errno = ENODEV;
        return NULL;
    }

    size_t len = strlen(path);
    if (len >= sizeof(((DIR *)0)->path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    DIR *d = malloc(sizeof(*d));
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(d->path, path, len + 1);
    d->start = 0;
    d->count = 0;
    d->index = 0;
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (!d)
        return NULL;

    if (d->index >= d->count) {
        uint32_t got = 0;
        if (FsdReadDir(&g_dir_conn, d->path, d->start, d->entries, DIR_BATCH, &got) != ZUZU_OK)
            return NULL;
        if (got == 0)
            return NULL;
        d->count = got;
        d->index = 0;
        d->start += got;
    }

    FsdDirEntry *e = &d->entries[d->index++];
    d->cur.d_type = e->type;
    size_t n = strlen(e->name);
    if (n >= sizeof(d->cur.d_name))
        n = sizeof(d->cur.d_name) - 1;
    memcpy(d->cur.d_name, e->name, n);
    d->cur.d_name[n] = '\0';
    return &d->cur;
}

int closedir(DIR *d)
{
    free(d);
    return 0;
}
