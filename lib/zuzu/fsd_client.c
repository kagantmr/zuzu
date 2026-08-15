#include <string.h>
#include <zuzu/fsd_client.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

Err FsdAttach(FsdConn *c, Handle port, Pid pid, uint32_t want_size)
{
    if (c->ready)
        return ZUZU_OK;

    /* page-align and clamp so the low bits are free for the packed slot */
    want_size = (want_size + FSD_SETBUF_MASK) & ~FSD_SETBUF_MASK;
    if (want_size < FSD_SHM_MIN)
        want_size = FSD_SHM_MIN;
    if (want_size > FSD_SHM_MAX)
        want_size = FSD_SHM_MAX;

    c->port = port;
    c->pid = pid;

    c->shm = ZuzuShmemCreate(want_size);
    if (c->shm < 0)
        return c->shm;

    void *p = ZuzuMemMap(c->shm, 0, PROT_RW, 0);
    if (ZuzuPtrIsErr(p))
        return (Err)(intptr_t)p;
    c->buf = (uint8_t *)p;
    c->size = want_size;

    Handle slot = ZuzuGrant(c->shm, c->pid, 0);
    if (slot < 0)
        return slot;

    Message s = ZuzuMsgCall(c->port, FSD_SET_BUF, FSD_SETBUF_PACK(slot, want_size), 0);
    if ((Err)s.w1 != ZUZU_OK)
        return (Err)s.w1;

    c->ready = true;
    return ZUZU_OK;
}

Err FsdConnect(FsdConn *c, uint32_t want_size)
{
    if (c->ready)
        return ZUZU_OK;

    /* lookup returns the granted port slot and fsd's pid */
    Pid fsd_pid;
    Handle h = LookupServiceWithPid("/svc/fsd", &fsd_pid);
    if (h < 0)
        return h;

    return FsdAttach(c, h, fsd_pid, want_size);
}

/* Fill the request header at FSD_REQ_OFF; returns the payload cursor. */
static FsdRequest *FsdPrepRequest(FsdConn *c, uint32_t cmd)
{
    FsdRequest *r = (FsdRequest *)(c->buf + FSD_REQ_OFF);
    memset(r, 0, sizeof(*r));
    r->size = sizeof(*r);
    r->cmd = cmd;
    r->data_off = FSD_DATA_OFF;
    return r;
}

Err FsdOpen(FsdConn *c, const char *path, uint32_t mode, uint32_t *fd)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    FsdRequest *r = FsdPrepRequest(c, FSD_OPEN);
    r->mode = mode;
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    Message m = ZuzuMsgCall(c->port, FSD_OPEN, 0, 0);
    if ((Err)m.w1 != ZUZU_OK)
        return (Err)m.w1;
    if (fd)
        *fd = m.w2;
    return ZUZU_OK;
}

Err FsdClose(FsdConn *c, uint32_t fd)
{
    Message m = ZuzuMsgCall(c->port, FSD_CLOSE, fd, 0);
    return (Err)m.w1;
}

Err FsdRead(FsdConn *c, uint32_t fd, void *dst, uint32_t count, uint32_t *got)
{
    uint32_t cap = c->size - FSD_DATA_OFF;
    if (count > cap)
        count = cap;
    if (count > 0xFFFFu)
        count = 0xFFFFu; /* count rides the high 16 bits of arg */

    Message m = ZuzuMsgCall(c->port, FSD_READ, (fd & 0xFFFFu) | (count << 16), 0);
    if ((Err)m.w1 != ZUZU_OK)
        return (Err)m.w1;

    uint32_t g = m.w2;
    if (g > count)
        g = count;
    if (g && dst)
        memcpy(dst, c->buf + FSD_DATA_OFF, g);
    if (got)
        *got = g;
    return ZUZU_OK;
}

Err FsdWrite(FsdConn *c, uint32_t fd, const void *src, uint32_t count, uint32_t *put)
{
    uint32_t cap = c->size - FSD_DATA_OFF;
    if (count > cap)
        count = cap;
    if (count > 0xFFFFu)
        count = 0xFFFFu;

    if (count)
        memcpy(c->buf + FSD_DATA_OFF, src, count);
    Message m = ZuzuMsgCall(c->port, FSD_WRITE, (fd & 0xFFFFu) | (count << 16), 0);
    if ((Err)m.w1 != ZUZU_OK)
        return (Err)m.w1;
    if (put)
        *put = m.w2;
    return ZUZU_OK;
}

Err FsdGetStat(FsdConn *c, const char *path, FsdStat *st)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    FsdRequest *r = FsdPrepRequest(c, FSD_STAT);
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    Message m = ZuzuMsgCall(c->port, FSD_STAT, 0, 0);
    if ((Err)m.w1 != ZUZU_OK)
        return (Err)m.w1;

    const FsdResponse *resp = (const FsdResponse *)(c->buf + FSD_RESP_OFF);
    if (st && resp->data_len >= sizeof(*st) && resp->data_off + sizeof(*st) <= c->size)
        memcpy(st, c->buf + resp->data_off, sizeof(*st));
    return ZUZU_OK;
}

Err FsdReadDir(FsdConn *c, const char *path, uint32_t start, FsdDirEntry *out, uint32_t max,
               uint32_t *count)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    FsdRequest *r = FsdPrepRequest(c, FSD_READDIR);
    r->offset = (int64_t)start;
    r->data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);

    Message m = ZuzuMsgCall(c->port, FSD_READDIR, 0, 0);
    if ((Err)m.w1 != ZUZU_OK)
        return (Err)m.w1;

    uint32_t got = m.w2;
    if (got > max)
        got = max;
    const FsdResponse *resp = (const FsdResponse *)(c->buf + FSD_RESP_OFF);
    if (out && got && (uint64_t)resp->data_off + (uint64_t)got * sizeof(FsdDirEntry) <= c->size)
        memcpy(out, c->buf + resp->data_off, (size_t)got * sizeof(FsdDirEntry));
    if (count)
        *count = got;
    return ZUZU_OK;
}
