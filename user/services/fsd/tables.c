#include "tables.h"
#include "backend/backend.h"
#include <zuzu/memprot.h>
#include <zuzu/umem.h>
#include <string.h>

static fsd_client_t client_table[FSD_MAX_CLIENTS];
static fsd_file_t   file_table[FSD_MAX_FILES];

static const fs_backend_t *g_backend;
static void *g_ctx;

void tables_init(const fs_backend_t *b, void *ctx) {
    g_backend = b;
    g_ctx = ctx;
}

err_t client_register(uint32_t pid, handle_t shm, uint32_t size)
{
    if (client_find(pid) != NULL) return ERR_DUPLICATE; /* already registered */
    if (size < FSD_SHM_MIN || size > FSD_SHM_MAX) return ERR_MALFORMED;
    for (int i = 0; i < FSD_MAX_CLIENTS; i++)
    {
        if (client_table[i].pid == 0)
        {
            void *p = zuzu_memmap(shm, 0, VM_PROT_RW, 0);
            if (zuzu_is_err(p)) return ERR_NOMEM;
            client_table[i].pid = pid;
            client_table[i].shm_handle = shm;
            client_table[i].buf = p;
            client_table[i].shm_size = size;
            return ZUZU_OK;
        }
    }
    return ERR_NOMEM; /* no free slot */
}

fsd_client_t *client_find(uint32_t pid)
{
    for (int i = 0; i < FSD_MAX_CLIENTS; i++)
        if (client_table[i].pid == pid) return &client_table[i];

    return NULL;
}

void client_drop(uint32_t pid)
{
    fsd_client_t *c = client_find(pid);
    if (!c) return;

    for (uint32_t fd = 0; fd < FSD_MAX_FILES; fd++)
        if (file_get(pid, fd)) file_close(pid, fd);

    if (c->buf) zuzu_memunmap(c->buf);
    memset(c, 0, sizeof(*c));
}

/* files */
err_t file_open(uint32_t pid, const char *path, uint32_t mode, uint32_t *fd_out) {
    fsd_client_t *client = client_find(pid);
    if (!client)
        return ERR_NOENT;

    size_t file_count = 0;
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (file_table[i].pid == pid) file_count++;

    if (file_count >= FSD_MAX_FILES_PER_CLIENT) return ERR_BUFFULL;

    /* lowest fd not in use by this pid */
    uint32_t fd = 0;
    while (fd < FSD_MAX_FILES && file_get(pid, fd)) fd++;
    if (fd == FSD_MAX_FILES) return ERR_BUFFULL;

    /* any free pool slot */
    int slot = -1;
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (file_table[i].pid == 0) { slot = i; break; }
    if (slot < 0) return ERR_BUFFULL;

    err_t rc = g_backend->open(g_ctx, file_table[slot].backend_file, path, mode);
    if (rc != ZUZU_OK) return rc;

    file_table[slot].pid = pid;
    file_table[slot].fd  = fd;
    *fd_out = fd;
    return ZUZU_OK;
}

void *file_get(uint32_t pid, uint32_t fd) {
    for (int i = 0; i < FSD_MAX_FILES; i++)
        if (file_table[i].pid == pid && file_table[i].fd == fd)
            return file_table[i].backend_file;
    return NULL;
}

err_t file_close(uint32_t pid, uint32_t fd) {
    for (int i = 0; i < FSD_MAX_FILES; i++) {
        if (file_table[i].pid == pid && file_table[i].fd == fd) {
            err_t rc = g_backend->close(g_ctx, file_table[i].backend_file);
            memset(&file_table[i], 0, sizeof(file_table[i]));
            return rc;
        }
    }
    return ERR_NOENT;
}
