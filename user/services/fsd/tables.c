#include "tables.h"
#include "backend/backend.h"
#include <zuzu/zuzu.h>
#include <string.h>

static FsdClient g_clients[FSD_MAX_CLIENTS];
static FsdFile g_files[FSD_MAX_FILES];

static const fs_backend_t *g_backend;
static void *g_ctx;

void TablesInit(const fs_backend_t *b, void *ctx)
{
    g_backend = b;
    g_ctx = ctx;
}

Err ClientRegister(Handle shm, Marker *badge)
{
    SvcResult q = HandleQuery(shm, QUERY_SIZE);
    if (q.r0 != ZUZU_OK)
        return (Err)q.r0;
    uint32_t size = (uint32_t)q.r1;
    if (size < FSD_SHM_MIN || size > FSD_SHM_MAX || (size & (FSD_PAGE_SIZE - 1)) != 0)
        return ERR_BADARG;
    /* rest unchanged */

    for (uint32_t i = 0; i < FSD_MAX_CLIENTS; i++)
    {
        FsdClient *c = &g_clients[i];
        if (c->in_use)
            continue;

        void *va = MemMap(shm, 0, PROT_RW);
        if (PtrIsErr(va))
            return ERR_NOMEM;

        uint32_t gen = (c->gen + 1) & 0xFFFFFFU;
        if (gen == 0)
            gen = 1;
        *c = (FsdClient){ .in_use = true, .gen = gen, .shm_handle = shm, .buf = va,
                          .shm_size = size };
        *badge = FSD_BADGE(gen, i);
        return ZUZU_OK;
    }
    return ERR_NOMEM;
}

FsdClient *ClientFind(Marker badge)
{
    if (badge == MARKER_NONE)
        return NULL;
    uint32_t slot = FSD_BADGE_SLOT(badge);
    if (slot >= FSD_MAX_CLIENTS)
        return NULL;
    FsdClient *c = &g_clients[slot];
    if (!c->in_use || c->gen != FSD_BADGE_GEN(badge))
        return NULL;
    return c;
}

uint32_t ClientSlot(const FsdClient *c)
{
    return (uint32_t)(c - g_clients);
}

void ClientDrop(FsdClient *c)
{
    uint32_t slot = ClientSlot(c);
    for (uint32_t fd = 0; fd < FSD_MAX_FILES; fd++)
        if (FileGet(slot, fd))
            FileClose(slot, fd);

    MemUnmap(c->buf);
    HandleClose(c->shm_handle);
    uint32_t gen = c->gen;
    memset(c, 0, sizeof(*c));
    c->gen = gen;
}

Err FileOpen(uint32_t slot, const char *path, uint32_t mode, uint32_t *fd_out)
{
    size_t file_count = 0;
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (g_files[i].in_use && g_files[i].slot == slot)
            file_count++;
    if (file_count >= FSD_MAX_FILES_PER_CLIENT)
        return ERR_BUFFULL;

    uint32_t fd = 0;
    while (fd < FSD_MAX_FILES && FileGet(slot, fd))
        fd++;
    if (fd == FSD_MAX_FILES)
        return ERR_BUFFULL;

    int idx = -1;
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (!g_files[i].in_use)
        {
            idx = i;
            break;
        }
    if (idx < 0)
        return ERR_BUFFULL;

    Err rc = g_backend->open(g_ctx, g_files[idx].backend_file, path, mode);
    if (rc != ZUZU_OK)
        return rc;

    g_files[idx].in_use = true;
    g_files[idx].slot = slot;
    g_files[idx].fd = fd;
    *fd_out = fd;
    return ZUZU_OK;
}

void *FileGet(uint32_t slot, uint32_t fd)
{
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (g_files[i].in_use && g_files[i].slot == slot && g_files[i].fd == fd)
            return g_files[i].backend_file;
    return NULL;
}

Err FileClose(uint32_t slot, uint32_t fd)
{
    for (int i = 0; i < FSD_MAX_FILES; i++)
    {
        if (g_files[i].in_use && g_files[i].slot == slot && g_files[i].fd == fd)
        {
            Err rc = g_backend->close(g_ctx, g_files[i].backend_file);
            memset(&g_files[i], 0, sizeof(g_files[i]));
            return rc;
        }
    }
    return ERR_NOENT;
}
