/**
 * fsd - filesystem daemon
 *
 * One daemon in front of a filesystem backend (backend/fat.c) that serves
 * clients over the protocol in include/fs/protocols/fsd.h.
 *
 * Startup order is load-bearing: the backend is mounted before the port is
 * published, so no client can send a request before we are able to serve it.
 */

#include "backend/backend.h"
#include "client_table.h"
#include "tables.h"
#include "zuzu/service.h"

#include <string.h>
#include <util/msg.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define FSD_PATH_MAX 256u
#define PORT_BIT 0
#define DEATH_BIT 1 /* every client's liveness port; clients never ring g_event */
#define POLL_MS 100

static Handle g_port = -1;
static Handle g_event = -1;
static const fs_backend_t *g_backend = &fat_backend;
static void *g_ctx = NULL;

static void CloseGrant(const PortWaitResult *r)
{
    if (r->granted >= 0)
        HandleClose(r->granted);
}

static void ReplyResponse(const FsdResponse *resp, Handle grant)
{
    memcpy(GetMessageBox(), resp, sizeof(*resp));
    Err rc = Reply(sizeof(*resp), grant);
    if (rc != ZUZU_OK)
        UserspaceDebugLog("fsd: reply failed: %d", (int)rc);
}

static void ReplyStatus(Err status)
{
    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size = sizeof(resp);
    resp.status = status;
    ReplyResponse(&resp, -1);
}

static Err ValidateRequest(const FsdClient *c, const FsdRequest *req)
{
    if (req->data_off < FSD_DATA_OFF || req->data_off > c->shm_size)
        return ERR_MALFORMED;
    if (req->cmd != FSD_READ && req->cmd != FSD_WRITE &&
        req->data_len > c->shm_size - req->data_off)
        return ERR_MALFORMED;
    return ZUZU_OK;
}

/* A client may fill the whole page with non-NUL bytes, so cap the copy by both
 * the destination and what is left in the page, then force termination. */
static void CopyShmString(const FsdClient *c, uint32_t off, char *dst, size_t dstsz)
{
    uint32_t avail = c->shm_size - off;
    uint32_t lim = (uint32_t)dstsz - 1u;
    if (avail < lim)
        lim = avail;
    strncpy(dst, (const char *)c->buf + off, lim);
    dst[lim] = '\0';
}

static Err CmdOpen(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    char path[FSD_PATH_MAX];
    CopyShmString(c, req->data_off, path, sizeof(path));

    uint32_t fd = 0;
    Err rc = FileOpen(ClientSlot(c), path, req->mode, &fd);
    resp->fd = fd;
    return rc;
}

static Err CmdClose(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    (void)resp;
    return FileClose(ClientSlot(c), req->fd);
}

static Err CmdSeek(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    void *file = FileGet(ClientSlot(c), req->fd);
    if (!file)
        return ERR_NOENT;

    int64_t newpos = 0;
    Err rc = g_backend->seek(g_ctx, file, req->offset, req->whence, &newpos);
    resp->offset = newpos;
    return rc;
}

static Err PutStat(FsdClient *c, const FsdStat *st, FsdResponse *resp)
{
    memcpy((uint8_t *)c->buf + FSD_DATA_OFF, st, sizeof(*st));
    resp->data_off = FSD_DATA_OFF;
    resp->data_len = sizeof(*st);
    return ZUZU_OK;
}

static Err CmdStat(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    char path[FSD_PATH_MAX];
    CopyShmString(c, req->data_off, path, sizeof(path));

    FsdStat st;
    memset(&st, 0, sizeof(st));
    Err rc = g_backend->stat(g_ctx, path, &st);
    if (rc == ZUZU_OK)
        PutStat(c, &st, resp);
    return rc;
}

static Err CmdFstat(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    void *file = FileGet(ClientSlot(c), req->fd);
    if (!file)
        return ERR_NOENT;

    /* The backend has no fstat(file); derive the size by seeking to END and
     * restoring the position. An open fd is always a regular file. */
    int64_t cur = 0, end = 0, tmp = 0;
    Err rc = g_backend->seek(g_ctx, file, 0, FSD_SEEK_CUR, &cur);
    if (rc == ZUZU_OK)
        rc = g_backend->seek(g_ctx, file, 0, FSD_SEEK_END, &end);
    if (rc == ZUZU_OK)
        rc = g_backend->seek(g_ctx, file, cur, FSD_SEEK_SET, &tmp);
    if (rc != ZUZU_OK)
        return rc;

    FsdStat st;
    memset(&st, 0, sizeof(st));
    st.size = (uint32_t)end;
    st.type = FSD_TYPE_FILE;
    return PutStat(c, &st, resp);
}

static Err CmdReadDir(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    char path[FSD_PATH_MAX];
    CopyShmString(c, req->data_off, path, sizeof(path));

    FsdDirEntry *out = (FsdDirEntry *)((uint8_t *)c->buf + FSD_DATA_OFF);
    uint32_t max = (c->shm_size - FSD_DATA_OFF) / sizeof(FsdDirEntry);

    uint32_t count = 0;
    Err rc = g_backend->readdir(g_ctx, path, (uint32_t)req->offset, out, max, &count);
    resp->count = count;
    if (rc == ZUZU_OK) {
        resp->data_off = FSD_DATA_OFF;
        resp->data_len = count * sizeof(FsdDirEntry);
    }
    return rc;
}

static Err CmdUnlink(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    (void)resp;
    char path[FSD_PATH_MAX];
    CopyShmString(c, req->data_off, path, sizeof(path));
    return g_backend->unlink(g_ctx, path);
}

static Err CmdRename(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    (void)resp;
    /* "from\0to\0": copy the declared payload, terminate, split on the first
     * NUL; the second path must fall wholly inside what was copied. */
    char buf[2u * FSD_PATH_MAX];
    uint32_t avail = c->shm_size - req->data_off;
    uint32_t lim = sizeof(buf) - 1u;
    if (avail < lim)
        lim = avail;
    if (req->data_len < lim)
        lim = req->data_len;
    memcpy(buf, (const uint8_t *)c->buf + req->data_off, lim);
    buf[lim] = '\0';

    size_t flen = strlen(buf);
    if (flen >= lim)
        return ERR_MALFORMED;
    const char *to = buf + flen + 1;
    if (*to == '\0')
        return ERR_MALFORMED;

    return g_backend->rename(g_ctx, buf, to);
}

static Err CmdRead(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    void *file = FileGet(ClientSlot(c), req->fd);
    if (!file)
        return ERR_NOENT;

    uint32_t count = req->data_len;
    uint32_t cap = c->shm_size - FSD_DATA_OFF;
    if (count > cap)
        count = cap;

    uint32_t got = 0;
    Err rc = g_backend->read(g_ctx, file, (uint8_t *)c->buf + FSD_DATA_OFF, count, &got);
    resp->count = got;
    if (rc == ZUZU_OK) {
        resp->data_off = FSD_DATA_OFF;
        resp->data_len = got;
    }
    return rc;
}

static Err CmdWrite(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    void *file = FileGet(ClientSlot(c), req->fd);
    if (!file)
        return ERR_NOENT;

    uint32_t count = req->data_len;
    uint32_t cap = c->shm_size - FSD_DATA_OFF;
    if (count > cap)
        count = cap;

    uint32_t put = 0;
    Err rc = g_backend->write(g_ctx, file, (const uint8_t *)c->buf + FSD_DATA_OFF, count, &put);
    resp->count = put;
    return rc;
}

static void HandleAttach(const PortWaitResult *r)
{
    if (r->granted < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }

    FsdRequest req;
    memcpy(&req, GetMessageBox(), sizeof(req));

    Marker badge = 0;
    Err rc = ClientRegister(r->granted, &badge);
    if (rc != ZUZU_OK) {
        HandleClose(r->granted);
        ReplyStatus(rc);
        return;
    }

    SvcResult dup = HandleDuplicate(g_port, PERM_SEND | PERM_TXFR, badge);
    if (dup.r0 != ZUZU_OK) {
        ClientDrop(ClientFind(badge));
        ReplyStatus((Err)dup.r0);
        return;
    }

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size = sizeof(resp);
    memcpy(GetMessageBox(), &resp, sizeof(resp));
    rc = Reply(sizeof(resp), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (rc != ZUZU_OK) {
        /* A failed grant already woke the caller with the error. */
        UserspaceDebugLog("fsd: attach reply failed: %d", (int)rc);
        ClientDrop(ClientFind(badge));
    }
}

static Err ReadIntoObject(FsdClient *c, const FsdRequest *req, Handle obj, FsdResponse *resp)
{
    void *file = FileGet(ClientSlot(c), req->fd);
    if (!file)
        return ERR_NOENT;

    SvcResult size = HandleQuery(obj, QUERY_SIZE);
    if (size.r0 != ZUZU_OK)
        return (Err)size.r0;
    if (req->data_off > (uint32_t)size.r1 || req->data_len > (uint32_t)size.r1 - req->data_off)
        return ERR_MALFORMED;

    uint8_t *va = MemMap(obj, 0, PROT_RW);
    if (PtrIsErr(va))
        return (Err)(intptr_t)va;

    uint32_t got = 0;
    Err rc = g_backend->read(g_ctx, file, va + req->data_off, req->data_len, &got);
    MemUnmap(va);
    resp->count = got;
    return rc;
}

static void HandleReadObj(const PortWaitResult *r, const FsdRequest *req)
{
    FsdClient *c = ClientFind(r->sender);
    if (!c) {
        CloseGrant(r);
        ReplyStatus(ERR_NOTCONN);
        return;
    }
    if (r->granted < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size = sizeof(resp);
    resp.status = ReadIntoObject(c, req, r->granted, &resp);
    HandleClose(r->granted);
    ReplyResponse(&resp, -1);
}

static void HandleWatch(const PortWaitResult *r)
{
    FsdClient *c = ClientFind(r->sender);
    if (!c) {
        CloseGrant(r);
        ReplyStatus(ERR_NOTCONN);
        return;
    }
    if (r->granted < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    if (c->live >= 0) {
        CloseGrant(r);
        ReplyStatus(ERR_DUPLICATE);
        return;
    }
    Err rc = Bind(EVENT_PORT, g_event, r->granted, DEATH_BIT);
    if (rc != ZUZU_OK) {
        CloseGrant(r);
        if (rc == ERR_DEAD)
            ClientDrop(c); /* the client died before it could be watched */
        ReplyStatus(rc);
        return;
    }
    c->live = r->granted;
    ReplyStatus(ZUZU_OK);
}

static Err Dispatch(FsdClient *c, const FsdRequest *req, FsdResponse *resp)
{
    switch (req->cmd) {
    case FSD_OPEN:
        return CmdOpen(c, req, resp);
    case FSD_CLOSE:
        return CmdClose(c, req, resp);
    case FSD_READ:
        return CmdRead(c, req, resp);
    case FSD_WRITE:
        return CmdWrite(c, req, resp);
    case FSD_SEEK:
        return CmdSeek(c, req, resp);
    case FSD_STAT:
        return CmdStat(c, req, resp);
    case FSD_FSTAT:
        return CmdFstat(c, req, resp);
    case FSD_READDIR:
        return CmdReadDir(c, req, resp);
    case FSD_UNLINK:
        return CmdUnlink(c, req, resp);
    case FSD_RENAME:
        return CmdRename(c, req, resp);
    default:
        return ERR_NOSYS;
    }
}

static void HandleRequest(const PortWaitResult *r)
{
    if (r->xlen < sizeof(FsdRequest)) {
        CloseGrant(r);
        ReplyStatus(ERR_MALFORMED);
        return;
    }

    FsdRequest req;
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (req.size < sizeof(req)) {
        CloseGrant(r);
        ReplyStatus(ERR_MALFORMED);
        return;
    }

    if (r->sender == MARKER_NONE) {
        if (req.cmd == FSD_ATTACH)
            HandleAttach(r);
        else {
            CloseGrant(r);
            ReplyStatus(ERR_NOPERM);
        }
        return;
    }

    if (req.cmd == FSD_WATCH) {
        HandleWatch(r);
        return;
    }
    if (req.cmd == FSD_READ_OBJ) {
        HandleReadObj(r, &req);
        return;
    }

    CloseGrant(r);
    FsdClient *c = ClientFind(r->sender);
    if (!c) {
        ReplyStatus(ERR_NOTCONN);
        return;
    }

    if (req.cmd == FSD_ATTACH) {
        ReplyStatus(ERR_DUPLICATE);
        return;
    }
    if (req.cmd == FSD_DETACH) {
        ClientDrop(c);
        ReplyStatus(ZUZU_OK);
        return;
    }

    FsdResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.size = sizeof(resp);
    Err rc = ValidateRequest(c, &req);
    if (rc == ZUZU_OK)
        rc = Dispatch(c, &req, &resp);
    resp.status = rc;
    ReplyResponse(&resp, -1);
}

int main(void)
{
    Err rc = g_backend->mount(&g_ctx);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("fsd: mount failed: %d", (int)rc);
        return 1;
    }
    TablesInit(g_backend, g_ctx);

    g_port = CreatePort();
    g_event = CreateEvent();
    if (g_port < 0 || g_event < 0) {
        UserspaceDebugLog("fsd: port/event create failed");
        return 1;
    }
    rc = Bind(EVENT_PORT, g_event, g_port, PORT_BIT);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("fsd: bind port failed: %d", (int)rc);
        return 1;
    }

    rc = RegisterService("/svc/fsd", g_port);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("fsd: register failed: %d", (int)rc);
        return 1;
    }

    UserspaceDebugLog("fsd: ready");

    for (;;) {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        if (ev.status == ZUZU_OK && (ev.bits & (1U << DEATH_BIT)))
            ClientsReapDead();

        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }
    }
}
