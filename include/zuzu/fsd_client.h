#ifndef ZUZU_FSD_CLIENT_H
#define ZUZU_FSD_CLIENT_H

/**
 * fsd_client.h - minimal client for the fsd protocol.
 *
 * fsd requires the client to own the shared buffer: create it, grant it to
 * fsd, and announce it with FSD_SET_BUF. This header wraps that handshake and
 * the per-command request/response marshalling so callers (sysd, zzsh, ...)
 * don't each re-implement it. Tier-1 only (uses zuzu string/mem helpers).
 */

#include <zuzu/zuzu.h>
#include <zuzu/memprot.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/fsd_protocol.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    int32_t  port;   /* granted handle to fsd's port                */
    uint32_t pid;    /* fsd's pid, needed to grant our buffer to it  */
    handle_t shm;    /* our shm handle                               */
    uint8_t *buf;    /* mapped base of the shared buffer             */
    uint32_t size;   /* buffer size (page-aligned)                   */
    bool     ready;
} fsd_conn_t;

/* Establish a session given fsd's already-resolved port handle and pid: create
 * a buffer, grant it to fsd, announce it with FSD_SET_BUF. Use this when the
 * caller cannot (or should not) resolve fsd over NT_PORT — notably sysd, which
 * *is* the nametable and would deadlock RPC-ing itself. Idempotent. */
static inline int32_t fsd_attach(fsd_conn_t *c, int32_t port, uint32_t pid,
                                 uint32_t want_size)
{
    if (c->ready)
        return ZUZU_OK;

    /* page-align and clamp so the low bits are free for the packed slot */
    want_size = (want_size + FSD_SETBUF_MASK) & ~FSD_SETBUF_MASK;
    if (want_size < FSD_SHM_MIN) want_size = FSD_SHM_MIN;
    if (want_size > FSD_SHM_MAX) want_size = FSD_SHM_MAX;

    c->port = port;
    c->pid  = pid;

    c->shm = zuzu_shm_create(want_size);
    if (c->shm < 0)
        return (int32_t)c->shm;

    void *p = zuzu_memmap(c->shm, 0, VM_PROT_RW, 0);
    if (zuzu_is_err(p))
        return (int32_t)(intptr_t)p;
    c->buf  = (uint8_t *)p;
    c->size = want_size;

    int32_t slot = zuzu_grant(c->shm, (int32_t)c->pid);
    if (slot < 0)
        return slot;

    msg_t s = zuzu_msg_call(c->port, FSD_SET_BUF, FSD_SETBUF_PACK(slot, want_size), 0);
    if ((int32_t)s.r1 != ZUZU_OK)
        return (int32_t)s.r1;

    c->ready = true;
    return ZUZU_OK;
}

/* Establish a session by resolving fsd over NT_PORT, then attaching. For
 * ordinary clients. Idempotent — ZUZU_OK immediately if already connected. */
static inline int32_t fsd_connect(fsd_conn_t *c, uint32_t want_size)
{
    if (c->ready)
        return ZUZU_OK;

    /* lookup returns the granted port slot in r2 and fsd's pid in r3 */
    msg_t l = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("fsd"), 0);
    if (l.r1 != NT_LU_OK)
        return (int32_t)l.r1;

    return fsd_attach(c, (int32_t)l.r2, l.r3, want_size);
}

/* Fill the request header at FSD_REQ_OFF; returns the payload cursor. */
static inline fsd_req_t *fsd__req(fsd_conn_t *c, uint32_t cmd)
{
    fsd_req_t *r = (fsd_req_t *)(c->buf + FSD_REQ_OFF);
    memset(r, 0, sizeof(*r));
    r->size     = sizeof(*r);
    r->cmd      = cmd;
    r->data_off = FSD_DATA_OFF;
    return r;
}

static inline int32_t fsd_open(fsd_conn_t *c, const char *path, uint32_t mode, uint32_t *fd)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    fsd_req_t *r = fsd__req(c, FSD_OPEN);
    r->mode     = mode;
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    msg_t m = zuzu_msg_call(c->port, FSD_OPEN, 0, 0);
    if ((int32_t)m.r1 != ZUZU_OK)
        return (int32_t)m.r1;
    if (fd) *fd = m.r2;
    return ZUZU_OK;
}

static inline int32_t fsd_close(fsd_conn_t *c, uint32_t fd)
{
    msg_t m = zuzu_msg_call(c->port, FSD_CLOSE, fd, 0);
    return (int32_t)m.r1;
}

/* Read up to `count` bytes into `dst`; *got set to the number returned. */
static inline int32_t fsd_read(fsd_conn_t *c, uint32_t fd, void *dst,
                               uint32_t count, uint32_t *got)
{
    uint32_t cap = c->size - FSD_DATA_OFF;
    if (count > cap)      count = cap;
    if (count > 0xFFFFu)  count = 0xFFFFu;   /* count rides the high 16 bits of arg */

    msg_t m = zuzu_msg_call(c->port, FSD_READ, (fd & 0xFFFFu) | (count << 16), 0);
    if ((int32_t)m.r1 != ZUZU_OK)
        return (int32_t)m.r1;

    uint32_t g = m.r2;
    if (g > count) g = count;
    if (g && dst) memcpy(dst, c->buf + FSD_DATA_OFF, g);
    if (got) *got = g;
    return ZUZU_OK;
}

static inline int32_t fsd_write(fsd_conn_t *c, uint32_t fd, const void *src,
                                uint32_t count, uint32_t *put)
{
    uint32_t cap = c->size - FSD_DATA_OFF;
    if (count > cap)      count = cap;
    if (count > 0xFFFFu)  count = 0xFFFFu;

    if (count) memcpy(c->buf + FSD_DATA_OFF, src, count);
    msg_t m = zuzu_msg_call(c->port, FSD_WRITE, (fd & 0xFFFFu) | (count << 16), 0);
    if ((int32_t)m.r1 != ZUZU_OK)
        return (int32_t)m.r1;
    if (put) *put = m.r2;
    return ZUZU_OK;
}

static inline int32_t fsd_stat(fsd_conn_t *c, const char *path, fsd_stat_t *st)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    fsd_req_t *r = fsd__req(c, FSD_STAT);
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    msg_t m = zuzu_msg_call(c->port, FSD_STAT, 0, 0);
    if ((int32_t)m.r1 != ZUZU_OK)
        return (int32_t)m.r1;

    const fsd_resp_t *resp = (const fsd_resp_t *)(c->buf + FSD_RESP_OFF);
    if (st && resp->data_len >= sizeof(*st) && resp->data_off + sizeof(*st) <= c->size)
        memcpy(st, c->buf + resp->data_off, sizeof(*st));
    return ZUZU_OK;
}

/* Read directory entries starting at `start`; *count set to entries returned. */
static inline int32_t fsd_readdir(fsd_conn_t *c, const char *path, uint32_t start,
                                  fsd_dirent_t *out, uint32_t max, uint32_t *count)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    fsd_req_t *r = fsd__req(c, FSD_READDIR);
    r->offset   = (int64_t)start;
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    msg_t m = zuzu_msg_call(c->port, FSD_READDIR, 0, 0);
    if ((int32_t)m.r1 != ZUZU_OK)
        return (int32_t)m.r1;

    uint32_t got = m.r2;
    if (got > max) got = max;
    const fsd_resp_t *resp = (const fsd_resp_t *)(c->buf + FSD_RESP_OFF);
    if (out && got &&
        (uint64_t)resp->data_off + (uint64_t)got * sizeof(fsd_dirent_t) <= c->size)
        memcpy(out, c->buf + resp->data_off, (size_t)got * sizeof(fsd_dirent_t));
    if (count) *count = got;
    return ZUZU_OK;
}

#endif /* ZUZU_FSD_CLIENT_H */
