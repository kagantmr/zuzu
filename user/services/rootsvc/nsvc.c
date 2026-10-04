#include "rootsvc.h"
#include <stdbool.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

Handle g_nsvc_port;

typedef struct {
    char path[NS_MAX_PATH];
    Handle handle; /* slot in nsvc's own table, regrantable */
    bool in_use;
} NsEntry;

static NsEntry g_registry[NS_MAX_SERVICES];

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
    memcpy(MessageBuf(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void NsvcRegister(const NsvcRequest *req, Handle granted)
{
    if (granted < 0) {
        ReplyStatus(ERR_BADARG); /* register must come with a port to grant */
        return;
    }

    int free_slot = -1;
    int found = -1;
    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (!g_registry[i].in_use) {
            if (free_slot == -1)
                free_slot = i;
            continue;
        }
        if (strncmp(g_registry[i].path, req->path, NS_MAX_PATH) == 0) {
            found = i;
            break;
        }
    }

    int slot = (found >= 0) ? found : free_slot;
    if (slot < 0) {
        ReplyStatus(ERR_NOMEM);
        return;
    }

    NsEntry *e = &g_registry[slot];
    strncpy(e->path, req->path, NS_MAX_PATH - 1);
    e->path[NS_MAX_PATH - 1] = '\0';
    e->handle = granted;
    e->in_use = true;
    ReplyStatus(ZUZU_OK);
}

static void NsvcLookup(const NsvcRequest *req)
{
    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (!g_registry[i].in_use)
            continue;
        if (strncmp(g_registry[i].path, req->path, NS_MAX_PATH) != 0)
            continue;

        SvcResult dup = HandleDuplicate(g_registry[i].handle, (PERM_SEND | PERM_TXFR), MARKER_NONE);
        if (dup.r0 == ZUZU_OK) {
            Reply(0, (Handle)dup.r1);
            return;
        }
        break;
    }
    Reply(0, -1); /* not found */
}

void NsvcMain(void)
{
    UserspaceDebugLog("nsvc: up");
    for (;;) {
        PortWaitResult result = FormatToPortWait(WaitOn(g_nsvc_port, TIMEOUT_INFINITE));
        if (result.status != ZUZU_OK)
            continue;

        NsvcRequest req;
        if (NsvcUnpack(MessageBuf(), result.xlen, &req) != ZUZU_OK) {
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