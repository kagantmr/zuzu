#include "rootsvc.h"
#include <malloc.h>
#include <stdbool.h>
#include <string.h>
#include <util/fnv1a.h>
#include <zuzu/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

Handle g_nsvc_port;

#define NS_BUCKETS 64u

typedef struct NsEntry {
    struct NsEntry *next;
    char path[NS_MAX_PATH];
    Handle handle; /* slot in nsvc's own table, regrantable */
} NsEntry;

static NsEntry *g_buckets[NS_BUCKETS];

static NsEntry **NsBucket(const char *path) { return &g_buckets[Fnv1aHash(path) % NS_BUCKETS]; }

static NsEntry *NsFind(const char *path)
{
    for (NsEntry *e = *NsBucket(path); e; e = e->next) {
        if (strncmp(e->path, path, NS_MAX_PATH) == 0)
            return e;
    }
    return NULL;
}

static Err NsvcUnpack(const char *buf, uint32_t xlen, NsvcRequest *out)
{
    if (xlen < 4)
        return ERR_BADARG;

    uint32_t cmd;
    memcpy(&cmd, buf, 4);

    uint32_t off = 4;
    if (off >= xlen)
        return ERR_BADARG;
    uint32_t remaining = xlen - off;
    size_t len = strnlen(buf + off, remaining);
    if (len == remaining)
        return ERR_BADARG; /* no NUL within bounds */
    if (len >= NS_MAX_PATH)
        return ERR_BADARG;

    out->cmd = (NsvcOpcode)cmd;
    out->path = (char *)(buf + off);
    return ZUZU_OK;
}

Handle NsvcInit(void)
{
    g_nsvc_port = CreatePort();
    return g_nsvc_port;
}

static void ReplyStatus(Err status)
{
    memcpy(GetMessageBox(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void NsvcRegister(const NsvcRequest *req, Handle granted)
{
    if (granted < 0) {
        ReplyStatus(ERR_BADARG); /* register must come with a port to grant */
        return;
    }

    NsEntry *e = NsFind(req->path);
    if (e) {
        HandleClose(e->handle);
    } else {
        e = malloc(sizeof(*e));
        if (!e) {
            HandleClose(granted);
            ReplyStatus(ERR_NOMEM);
            return;
        }
        strncpy(e->path, req->path, NS_MAX_PATH - 1);
        e->path[NS_MAX_PATH - 1] = '\0';
        NsEntry **bucket = NsBucket(e->path);
        e->next = *bucket;
        *bucket = e;
    }

    e->handle = granted;
    ReplyStatus(ZUZU_OK);
}

static void NsvcLookup(const NsvcRequest *req)
{
    NsEntry *e = NsFind(req->path);
    if (e) {
        SvcResult dup = HandleDuplicate(e->handle, (PERM_SEND | PERM_TXFR), MARKER_NONE);
        if (dup.r0 == ZUZU_OK) {
            Reply(0, (Handle)dup.r1);
            return;
        }
    }
    Reply(0, -1); /* not found */
}

void NsvcMain(void *arg)
{
    (void)arg;
    UserspaceDebugLog("nsvc: up");
    for (;;) {
        PortWaitResult result = FormatToPortWait(WaitOn(g_nsvc_port, TIMEOUT_INFINITE));
        if (result.status != ZUZU_OK)
            continue;

        NsvcRequest req;
        if (NsvcUnpack(GetMessageBox(), result.xlen, &req) != ZUZU_OK) {
            ReplyStatus(ERR_BADARG);
            continue;
        }

        switch (req.cmd) {
        case NS_REGISTER:
            NsvcRegister(&req, result.granted);
            break;
        case NS_LOOKUP:
            NsvcLookup(&req);
            break;
        default:
            ReplyStatus(ERR_BADARG);
            break;
        }
    }
}