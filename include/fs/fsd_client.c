#include <string.h>
#include <fs/fsd_client.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

static Err FsdCall(Handle port, const FsdRequest *req, FsdResponse *resp, Handle grant, Handle *granted)
{
    memcpy(MessageBuf(), req, sizeof(*req));
    SvcResult r = Call(port, sizeof(*req), grant);
    if (granted)
        *granted = (Handle)r.r3;
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    if ((uint32_t)r.r1 < sizeof(*resp))
        return ERR_MALFORMED;
    memcpy(resp, MessageBuf(), sizeof(*resp));
    return resp->status;
}

static void FsdInitRequest(FsdRequest *r, uint32_t cmd)
{
    memset(r, 0, sizeof(*r));
    r->size = sizeof(*r);
    r->cmd = cmd;
    r->data_off = FSD_DATA_OFF;
}

static Err FsdTransact(FsdConn *c, const FsdRequest *req, FsdResponse *resp)
{
    return FsdCall(c->port, req, resp, -1, NULL);
}

static void FsdTeardown(FsdConn *c)
{
    if (c->buf)
        MemUnmap(c->buf);
    if (c->shm >= 0)
        HandleClose(c->shm);
    c->buf = NULL;
    c->shm = -1;
    c->port = -1;
    if (c->live >= 0)
        HandleClose(c->live);
    c->live = -1;
}

Err FsdAttach(FsdConn *c, Handle port, Spid pid, uint32_t want_size)
{
    (void)pid;
    if (c->ready)
        return ZUZU_OK;
    c->live = -1;

    want_size = (want_size + FSD_PAGE_SIZE - 1) & ~(FSD_PAGE_SIZE - 1);
    if (want_size < FSD_SHM_MIN)
        want_size = FSD_SHM_MIN;
    if (want_size > FSD_SHM_MAX)
        want_size = FSD_SHM_MAX;

    c->shm = CreateMem(want_size / FSD_PAGE_SIZE);
    if (c->shm < 0)
        return c->shm;

    void *va = MemMap(c->shm, 0, PROT_RW);
    if (PtrIsErr(va))
    {
        HandleClose(c->shm);
        c->shm = -1;
        return (Err)va;
    }
    c->buf = (uint8_t *)va;
    c->size = want_size;

    SvcResult dup = HandleDuplicate(c->shm, PERM_MAP | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK)
    {
        FsdTeardown(c);
        return (Err)dup.r0;
    }

    FsdRequest req;
    FsdInitRequest(&req, FSD_ATTACH);
    req.data_len = want_size;
    FsdResponse resp;
    Handle badged = -1;
    Err rc = FsdCall(port, &req, &resp, (Handle)dup.r1, &badged);
    HandleClose((Handle)dup.r1);
    if (rc == ZUZU_OK && badged < 0)
        rc = ERR_MALFORMED;
    if (rc != ZUZU_OK)
    {
        if (badged >= 0)
            HandleClose(badged);
        FsdTeardown(c);
        return rc;
    }

    c->port = badged;
    c->ready = true;

    /* Best effort: without it fsd only frees the session on FSD_DETACH. */
    c->live = CreatePort();
    if (c->live >= 0)
    {
        SvcResult watch = HandleDuplicate(c->live, PERM_WAIT | PERM_TXFR, MARKER_NONE);
        Err wrc = (Err)watch.r0;
        if (wrc == ZUZU_OK)
        {
            FsdRequest wreq;
            FsdInitRequest(&wreq, FSD_WATCH);
            FsdResponse wresp;
            wrc = FsdCall(c->port, &wreq, &wresp, (Handle)watch.r1, NULL);
            HandleClose((Handle)watch.r1);
        }
        if (wrc != ZUZU_OK)
        {
            HandleClose(c->live);
            c->live = -1;
        }
    }
    return ZUZU_OK;
}

Err FsdConnect(FsdConn *c, uint32_t want_size)
{
    if (c->ready)
        return ZUZU_OK;

    Handle h = LookupService("/svc/fsd");
    if (h < 0)
        return h;

    Err rc = FsdAttach(c, h, 0, want_size);
    HandleClose(h);
    return rc;
}

Err FsdDetach(FsdConn *c)
{
    if (!c->ready)
        return ZUZU_OK;

    FsdRequest req;
    FsdInitRequest(&req, FSD_DETACH);
    FsdResponse resp;
    Err rc = FsdTransact(c, &req, &resp);
    HandleClose(c->port);
    FsdTeardown(c);
    c->ready = false;
    return rc;
}

static Err FsdPathCall(FsdConn *c, uint32_t cmd, const char *path, uint32_t start, uint32_t mode,
                       FsdResponse *resp)
{
    size_t n = strlen(path);
    if (FSD_DATA_OFF + n + 1 > c->size)
        return ERR_OVERFLOW;

    FsdRequest req;
    FsdInitRequest(&req, cmd);
    req.offset = (int64_t)start;
    req.mode = mode;
    req.data_len = (uint32_t)n + 1;
    memcpy(c->buf + FSD_DATA_OFF, path, n + 1);
    return FsdTransact(c, &req, resp);
}

Err FsdOpen(FsdConn *c, const char *path, uint32_t mode, uint32_t *fd)
{
    FsdResponse resp;
    Err rc = FsdPathCall(c, FSD_OPEN, path, 0, mode, &resp);
    if (rc != ZUZU_OK)
        return rc;
    if (fd)
        *fd = resp.fd;
    return ZUZU_OK;
}

Err FsdClose(FsdConn *c, uint32_t fd)
{
    FsdRequest req;
    FsdInitRequest(&req, FSD_CLOSE);
    req.fd = fd;
    FsdResponse resp;
    return FsdTransact(c, &req, &resp);
}

Err FsdRead(FsdConn *c, uint32_t fd, void *dst, uint32_t count, uint32_t *got)
{
    uint32_t cap = c->size - FSD_DATA_OFF;
    if (count > cap)
        count = cap;

    FsdRequest req;
    FsdInitRequest(&req, FSD_READ);
    req.fd = fd;
    req.data_len = count;
    FsdResponse resp;
    Err rc = FsdTransact(c, &req, &resp);
    if (rc != ZUZU_OK)
        return rc;

    uint32_t g = resp.count;
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

    if (count)
        memcpy(c->buf + FSD_DATA_OFF, src, count);

    FsdRequest req;
    FsdInitRequest(&req, FSD_WRITE);
    req.fd = fd;
    req.data_len = count;
    FsdResponse resp;
    Err rc = FsdTransact(c, &req, &resp);
    if (rc != ZUZU_OK)
        return rc;
    if (put)
        *put = resp.count;
    return ZUZU_OK;
}

Err FsdSeek(FsdConn *c, uint32_t fd, int64_t offset, uint32_t whence, int64_t *newpos)
{
    FsdRequest req;
    FsdInitRequest(&req, FSD_SEEK);
    req.fd = fd;
    req.offset = offset;
    req.whence = whence;
    FsdResponse resp;
    Err rc = FsdTransact(c, &req, &resp);
    if (rc != ZUZU_OK)
        return rc;
    if (newpos)
        *newpos = resp.offset;
    return ZUZU_OK;
}

Err FsdGetStat(FsdConn *c, const char *path, FsdStat *st)
{
    FsdResponse resp;
    Err rc = FsdPathCall(c, FSD_STAT, path, 0, 0, &resp);
    if (rc != ZUZU_OK)
        return rc;

    if (st && resp.data_len >= sizeof(*st) && resp.data_off + sizeof(*st) <= c->size)
        memcpy(st, c->buf + resp.data_off, sizeof(*st));
    return ZUZU_OK;
}

Err FsdFstat(FsdConn *c, uint32_t fd, FsdStat *st)
{
    FsdRequest req;
    FsdInitRequest(&req, FSD_FSTAT);
    req.fd = fd;
    FsdResponse resp;
    Err rc = FsdTransact(c, &req, &resp);
    if (rc != ZUZU_OK)
        return rc;

    if (st && resp.data_len >= sizeof(*st) && resp.data_off + sizeof(*st) <= c->size)
        memcpy(st, c->buf + resp.data_off, sizeof(*st));
    return ZUZU_OK;
}

Err FsdReadDir(FsdConn *c, const char *path, uint32_t start, FsdDirEntry *out, uint32_t max,
               uint32_t *count)
{
    FsdResponse resp;
    Err rc = FsdPathCall(c, FSD_READDIR, path, start, 0, &resp);
    if (rc != ZUZU_OK)
        return rc;

    uint32_t got = resp.count;
    if (got > max)
        got = max;
    if (out && got && (uint64_t)resp.data_off + ((uint64_t)got * sizeof(FsdDirEntry)) <= c->size)
        memcpy(out, c->buf + resp.data_off, (size_t)got * sizeof(FsdDirEntry));
    if (count)
        *count = got;
    return ZUZU_OK;
}

Err FsdUnlink(FsdConn *c, const char *path)
{
    FsdResponse resp;
    return FsdPathCall(c, FSD_UNLINK, path, 0, 0, &resp);
}
