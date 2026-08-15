/**
 * fsd - filesystem daemon (zuzuOS v0.6 VFS)
 *
 * Replaces the old fat32d + fbox two-process chain with a single daemon that
 * talks to a storage driver through a filesystem backend shim (backend/fat.c)
 * and serves clients over the fsd protocol (include/zuzu/protocols/fsd_protocol.h).
 *
 * Startup order is load-bearing: the backend is mounted and the tables are
 * wired up *before* the port is published to the nametable, so no client can
 * send a request before we are able to serve it. (The old fat32d registered
 * first, a latent bug.)
 */

#include "tables.h"
#include "backend/backend.h"
#include "client_table.h"
#include "zuzu/service.h"

#include <zuzu/zuzu.h>
#include <zuzu/log.h>
#include <zuzu/protocols/nametable.h>
#include <string.h>

#define LOG_TAG "fsd"

#define FSD_PATH_MAX 256u

static int32_t            g_port    = -1;
static const fs_backend_t *g_backend = &fat_backend;
static void               *g_ctx     = NULL;

/* ------------------------------------------------------------------ *
 *  Request helpers
 * ------------------------------------------------------------------ */

/*
 * Copy the FsdRequest out of the client's shm and validate it. w2 (cmd) is
 * authoritative: we copy the struct once, then reject if its cmd field does not
 * match, so a concurrently-mutating client cannot slip a different command past
 * us (TOCTOU). Every field we later act on comes from this private copy, never a
 * second read of shm.
 */
static Err load_req(fsd_client_t *c, uint32_t cmd, FsdRequest *out)
{
    if (!c->buf) return ERR_NOTCONN;

    memcpy(out, (const uint8_t *)c->buf + FSD_REQ_OFF, sizeof(*out)); /* copy first */

    if (out->size < sizeof(*out) || out->size > FSD_RESP_OFF) return ERR_MALFORMED;
    if (out->cmd != cmd) return ERR_MALFORMED;                /* w2 authoritative */
    if (out->data_off < FSD_DATA_OFF) return ERR_MALFORMED;
    if (out->data_off > c->shm_size) return ERR_MALFORMED;
    if (out->data_len > c->shm_size - out->data_off) return ERR_MALFORMED; /* overflow-safe */
    return ZUZU_OK;
}

/*
 * Copy a NUL-terminated string out of shm at `off` into a bounded local buffer.
 * A client may fill the whole payload with non-NUL bytes, so we cap the copy by
 * both the destination size and the bytes actually left in shm (off is already
 * known <= shm_size), then force termination.
 */
static void shm_copy_str(const fsd_client_t *c, uint32_t off, char *dst, size_t dstsz)
{
    uint32_t avail = c->shm_size - off;
    uint32_t lim   = (uint32_t)dstsz - 1u;
    if (avail < lim) lim = avail;
    strncpy(dst, (const char *)c->buf + off, lim);
    dst[lim] = '\0';
}

/* Stage the FsdResponse extras into shm at FSD_RESP_OFF. */
static void put_resp(fsd_client_t *c, const FsdResponse *r)
{
    memcpy((uint8_t *)c->buf + FSD_RESP_OFF, r, sizeof(*r));
}

/* ------------------------------------------------------------------ *
 *  Command handlers
 * ------------------------------------------------------------------ */

/* The only command valid without an existing client entry. */
static void handle_set_buf(uint32_t reply_h, uint32_t sender, uint32_t arg)
{
    uint32_t slot = FSD_SETBUF_SLOT(arg);
    uint32_t size = FSD_SETBUF_SIZE(arg);

    Err rc = client_register(sender, (Handle)slot, size);
    ZuzuMsgReply(reply_h, (uint32_t)rc, 0, 0);
}

static void handle_open(uint32_t reply_h, uint32_t sender, fsd_client_t *c, const FsdRequest *req)
{
    char path[FSD_PATH_MAX];
    shm_copy_str(c, req->data_off, path, sizeof(path));

    uint32_t fd = 0;
    Err rc = file_open(sender, path, req->mode, &fd);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    resp.fd     = fd;
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, fd, 0);
}

static void handle_seek(uint32_t reply_h, uint32_t sender, fsd_client_t *c, const FsdRequest *req)
{
    void *file = file_get(sender, req->fd);
    if (!file) { ZuzuMsgReply(reply_h, (uint32_t)ERR_NOENT, 0, 0); return; }

    int64_t newpos = 0;
    Err rc = g_backend->seek(g_ctx, file, req->offset, req->whence, &newpos);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    resp.offset = newpos;
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, (uint32_t)newpos, 0);
}

static void handle_stat(uint32_t reply_h, fsd_client_t *c, const FsdRequest *req)
{
    char path[FSD_PATH_MAX];
    shm_copy_str(c, req->data_off, path, sizeof(path));

    FsdStat st;
    memset(&st, 0, sizeof(st));
    Err rc = g_backend->stat(g_ctx, path, &st);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    if (rc == ZUZU_OK) {
        memcpy((uint8_t *)c->buf + FSD_DATA_OFF, &st, sizeof(st));
        resp.data_off = FSD_DATA_OFF;
        resp.data_len = sizeof(st);
    }
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, st.size, 0);
}

static void handle_fstat(uint32_t reply_h, uint32_t sender, fsd_client_t *c, uint32_t fd)
{
    void *file = file_get(sender, fd);
    if (!file) { ZuzuMsgReply(reply_h, (uint32_t)ERR_NOENT, 0, 0); return; }

    /* The backend has no fstat(file); derive the size by seeking to END and
     * restoring the position. An open fd is always a regular file. */
    int64_t cur = 0, end = 0, tmp = 0;
    Err rc = g_backend->seek(g_ctx, file, 0, FSD_SEEK_CUR, &cur);
    if (rc == ZUZU_OK) rc = g_backend->seek(g_ctx, file, 0, FSD_SEEK_END, &end);
    if (rc == ZUZU_OK) rc = g_backend->seek(g_ctx, file, cur, FSD_SEEK_SET, &tmp);

    FsdStat st;
    memset(&st, 0, sizeof(st));
    if (rc == ZUZU_OK) {
        st.size = (uint32_t)end;
        st.type = FSD_TYPE_FILE;
    }

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    if (rc == ZUZU_OK) {
        memcpy((uint8_t *)c->buf + FSD_DATA_OFF, &st, sizeof(st));
        resp.data_off = FSD_DATA_OFF;
        resp.data_len = sizeof(st);
    }
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, st.size, 0);
}

static void handle_readdir(uint32_t reply_h, fsd_client_t *c, const FsdRequest *req)
{
    char path[FSD_PATH_MAX];
    shm_copy_str(c, req->data_off, path, sizeof(path));

    /* dirents land at FSD_DATA_OFF; bound the count by what actually fits in
     * this client's buffer, never a constant. */
    FsdDirEntry *out = (FsdDirEntry *)((uint8_t *)c->buf + FSD_DATA_OFF);
    uint32_t max = (c->shm_size - FSD_DATA_OFF) / sizeof(FsdDirEntry);
    uint32_t start = (uint32_t)req->offset;

    uint32_t count = 0;
    Err rc = g_backend->readdir(g_ctx, path, start, out, max, &count);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    resp.count  = count;
    if (rc == ZUZU_OK) {
        resp.data_off = FSD_DATA_OFF;
        resp.data_len = count * sizeof(FsdDirEntry);
    }
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, count, 0);
}

static void handle_unlink(uint32_t reply_h, fsd_client_t *c, const FsdRequest *req)
{
    char path[FSD_PATH_MAX];
    shm_copy_str(c, req->data_off, path, sizeof(path));

    Err rc = g_backend->unlink(g_ctx, path);
    ZuzuMsgReply(reply_h, (uint32_t)rc, 0, 0);
}

static void handle_rename(uint32_t reply_h, fsd_client_t *c, const FsdRequest *req)
{
    /* Two NUL-separated paths ("from\0to\0") in the payload region. Copy the
     * declared payload into a local buffer, terminate, then split on the first
     * NUL — the second path must fall wholly inside what we copied. */
    char buf[2u * FSD_PATH_MAX];
    uint32_t avail = c->shm_size - req->data_off;
    uint32_t lim   = sizeof(buf) - 1u;
    if (avail < lim)        lim = avail;
    if (req->data_len < lim) lim = req->data_len;
    memcpy(buf, (const uint8_t *)c->buf + req->data_off, lim);
    buf[lim] = '\0';

    size_t flen = strlen(buf);
    if (flen >= lim) { ZuzuMsgReply(reply_h, (uint32_t)ERR_MALFORMED, 0, 0); return; }
    const char *from = buf;
    const char *to   = buf + flen + 1;
    if (*to == '\0')  { ZuzuMsgReply(reply_h, (uint32_t)ERR_MALFORMED, 0, 0); return; }

    Err rc = g_backend->rename(g_ctx, from, to);
    ZuzuMsgReply(reply_h, (uint32_t)rc, 0, 0);
}

static void handle_close(uint32_t reply_h, uint32_t sender, uint32_t fd)
{
    Err rc = file_close(sender, fd);
    ZuzuMsgReply(reply_h, (uint32_t)rc, 0, 0);
}

static void handle_read(uint32_t reply_h, uint32_t sender, fsd_client_t *c, uint32_t arg)
{
    uint32_t fd    = arg & 0xFFFFu;
    uint32_t count = arg >> 16;

    void *file = file_get(sender, fd);
    if (!file) { ZuzuMsgReply(reply_h, (uint32_t)ERR_NOENT, 0, 0); return; }

    uint32_t cap = c->shm_size - FSD_DATA_OFF;   /* payload space, this client's buffer */
    if (count > cap) count = cap;

    uint32_t got = 0;
    Err rc = g_backend->read(g_ctx, file, (uint8_t *)c->buf + FSD_DATA_OFF, count, &got);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    resp.count  = got;
    if (rc == ZUZU_OK) {
        resp.data_off = FSD_DATA_OFF;
        resp.data_len = got;
    }
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, got, 0);
}

static void handle_write(uint32_t reply_h, uint32_t sender, fsd_client_t *c, uint32_t arg)
{
    uint32_t fd    = arg & 0xFFFFu;
    uint32_t count = arg >> 16;

    void *file = file_get(sender, fd);
    if (!file) { ZuzuMsgReply(reply_h, (uint32_t)ERR_NOENT, 0, 0); return; }

    uint32_t cap = c->shm_size - FSD_DATA_OFF;
    if (count > cap) count = cap;

    uint32_t put = 0;
    Err rc = g_backend->write(g_ctx, file, (const uint8_t *)c->buf + FSD_DATA_OFF, count, &put);

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size   = sizeof(resp);
    resp.status = rc;
    resp.count  = put;
    put_resp(c, &resp);

    ZuzuMsgReply(reply_h, (uint32_t)rc, put, 0);
}

/* ------------------------------------------------------------------ *
 *  Dispatch
 * ------------------------------------------------------------------ */

/* Run a shm-path command: pull+validate the request, then invoke `fn`. */
#define DISPATCH_SHM(fn, ...)                                             \
    do {                                                                 \
        FsdRequest req;                                                    \
        Err lrc = load_req(c, cmd, &req);                              \
        if (lrc != ZUZU_OK) { ZuzuMsgReply(reply_h, (uint32_t)lrc, 0, 0); return; } \
        fn(__VA_ARGS__);                                                 \
    } while (0)

static void dispatch(uint32_t reply_h, uint32_t sender, uint32_t cmd, uint32_t arg)
{
    if (cmd == FSD_SET_BUF) {
        handle_set_buf(reply_h, sender, arg);
        return;
    }

    fsd_client_t *c = client_find(sender);
    if (!c) { ZuzuMsgReply(reply_h, (uint32_t)ERR_NOTCONN, 0, 0); return; }

    switch (cmd) {
    /* shm-path: arg unused, request struct in shm */
    case FSD_OPEN:    DISPATCH_SHM(handle_open,    reply_h, sender, c, &req); break;
    case FSD_SEEK:    DISPATCH_SHM(handle_seek,    reply_h, sender, c, &req); break;
    case FSD_STAT:    DISPATCH_SHM(handle_stat,    reply_h, c, &req);         break;
    case FSD_READDIR: DISPATCH_SHM(handle_readdir, reply_h, c, &req);         break;
    case FSD_UNLINK:  DISPATCH_SHM(handle_unlink,  reply_h, c, &req);         break;
    case FSD_RENAME:  DISPATCH_SHM(handle_rename,  reply_h, c, &req);         break;

    /* register-path: arg carries fd (| count<<16 for read/write) */
    case FSD_CLOSE:   handle_close(reply_h, sender, arg);        break;
    case FSD_READ:    handle_read(reply_h, sender, c, arg);      break;
    case FSD_WRITE:   handle_write(reply_h, sender, c, arg);     break;
    case FSD_FSTAT:   handle_fstat(reply_h, sender, c, arg);     break;

    default:
        ZuzuMsgReply(reply_h, (uint32_t)ERR_NOSYS, 0, 0);
        break;
    }
}

/* ------------------------------------------------------------------ *
 *  Entry
 * ------------------------------------------------------------------ */

int main(void)
{
    /* 1. mount the backend before anything else can reach us. */
    Err rc = g_backend->mount(&g_ctx);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "mount failed: %d", (int)rc);
        return 1;
    }
    tables_init(g_backend, g_ctx);

    /* 2. create the port and publish it globally (den 0) only now that the
     * filesystem is serviceable. sysd adds fsd to the "disk" den so our
     * backend's diskio can reach pl181drv. */
    g_port = ZuzuPortCreate();
    if (g_port < 0) {
        LOG_ERROR(LOG_TAG, "port create failed: %d", (int)g_port);
        return 1;
    }

    RegisterService("/svc/fsd", g_port);

    LOG_INFO(LOG_TAG, "ready");

    /* 3. serve. */
    Handle handles[1] = { (Handle)g_port };
    while (1) {
        WaitanyResult res;
        if (ZuzuWaitany(handles, 1, TIMEOUT_INFINITE, &res) < 0)
            continue;
        if (res.kind != WAITANY_KIND_CALL)
            continue;   /* fsd is call-only; ignore stray sends/notifications */

        dispatch(res.source, res.w1, res.w2, res.w3);
    }

    return 0;
}
