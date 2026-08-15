#include "zuzu/err.h"
#include "zuzu/lmsg.h"
#include "zuzu/protocols/nametable.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/cap.h>
#include <zuzu/msg.h>
#include <zuzu/types.h>

typedef struct
{
    char path[NT_MAX_PATH];
    Handle handle; /* slot in OUR table, regrantable */
    Label label;   /* owner identity from WaitanyResult, never the request */
    Pid pid;       /* for scrub + LOOKUP_PID */
    bool in_use;
} NtEntry;

static NtEntry registry[NT_MAX_SERVICES];
static Label sysd_label;

static uint32_t Fnv1aHash(const char *s)
{
    uint32_t h = 0x811c9dc5u; // FNV offset basis
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 0x01000193u; // FNV prime
    }
    return h;
}

static int NtUnpack(const char *buf, uint32_t xlen, NtRequest *out)
{
    /* 1. Header must fit */
    if (xlen < 12)
    {
        return ERR_BADARG;
    }

    uint32_t cmd;
    Handle h;
    Pid p;
    memcpy(&cmd, buf, 4);
    memcpy(&h, buf + 4, 4);
    memcpy(&p, buf + 8, 4);

    uint32_t off = 12;
    if (off >= xlen)
        return ERR_BADARG;
    uint32_t remaining = xlen - off;
    size_t len = strnlen(buf + off, remaining);
    if (len == remaining)
        return ERR_BADARG; /* no NUL in bounds */
    if (len >= NT_MAX_PATH)
        return ERR_BADARG; /* fail loudly on over-long */
    out->path = buf + off;
    off += (uint32_t)len + 1;

    if (off != xlen)
        return ERR_BADARG;

    out->cmd = (NtOpcode)cmd;
    out->handle = h;
    out->pid = p;
    return ZUZU_OK;
}

static void NtRegister(NtRequest *req, Label label, Pid caller, Message *reply)
{

    if (label == LABEL_NONE) { reply->w1 = ERR_NOPERM; return; }

    int free_slot = -1;

    for (int i = 0; i < NT_MAX_SERVICES; i++)
    {
        if (!registry[i].in_use)
        {
            if (free_slot == -1)
                free_slot = i;
            continue;
        }
        if (strncmp(registry[i].path, req->path, NT_MAX_PATH) == 0)
        {
            // path already registered
            NtEntry *e = &registry[i];
            if (e->label != label)
            {
                reply->w1 = ERR_BUSY;
                return;
            }
            e->handle = req->handle;
            e->pid = caller;
            e->in_use = true;
            reply->w1 = ZUZU_OK;
            return;
        }
    }

    if (free_slot == -1)
    {
        reply->w1 = ERR_NOMEM; // table full
        return;
    }

    NtEntry *e = &registry[free_slot];
    strncpy(e->path, req->path, NT_MAX_PATH - 1);
    e->path[NT_MAX_PATH - 1] = '\0';
    e->handle = req->handle;
    e->label = label;
    e->pid = caller;
    e->in_use = true;

    reply->w1 = ZUZU_OK;
}

static void NtLookup(NtRequest *req, Label label, Pid caller, Message *reply)
{
    (void)label;
    for (int i = 0; i < NT_MAX_SERVICES; i++)
    {
        if (!registry[i].in_use)
            continue;
        if (strncmp(registry[i].path, req->path, NT_MAX_PATH) != 0)
            continue;

        Handle granted = ZuzuGrant(registry[i].handle, caller, 0);
        if (granted < 0)
        {
            if (granted == ERR_DEAD || granted == ERR_BADHANDLE)
                registry[i].in_use = false;   /* entry genuinely stale */
            reply->w1 = granted;
            return;
        }
        reply->w1 = ZUZU_OK;
        reply->w2 = (uint32_t)granted;
        reply->w3 = (uint32_t)registry[i].pid;
        return;
    }
    reply->w1 = ERR_NOENT; // no such service
}

static void NtLookupPid(NtRequest *req, Label label, Pid caller, Message *reply)
{
    (void)label; (void)caller;
    for (int i = 0; i < NT_MAX_SERVICES; i++)
    {
        if (!registry[i].in_use || registry[i].pid != req->pid)
            continue;

        reply->w1 = ZUZU_OK;
        reply->w2 = (uint32_t)registry[i].handle;
        reply->w3 = (uint32_t)registry[i].pid;
        return;

    }
    reply->w1 = ERR_NOENT;
}

static void NtScrubPid(NtRequest *req, Label label, Pid caller, Message *reply)
{
    (void)caller;
    if (label != sysd_label)
    {
        reply->w1 = ERR_NOPERM;
        return;
    }
    int found = 0;
    for (int i = 0; i < NT_MAX_SERVICES; i++)
    {
        if (registry[i].in_use && registry[i].pid == req->pid)
        {
            registry[i].in_use = false;
            found++;
        }
    }
    reply->w1 = found ? ZUZU_OK : ERR_NOENT;
}

int main(void)
{

    sysd_label = Fnv1aHash("/svc/sysd");

    while (1)
    {
        Handle handles[1] = {NT_PORT};
        WaitanyResult res;

        Err rc = ZuzuWaitany(handles, 1, TIMEOUT_INFINITE, &res);
        if (rc != ZUZU_OK)
        {
          if (rc == ERR_DEAD)
                continue;
            return rc;
        }

        if (res.kind == WAITANY_KIND_CALL || res.kind == WAITANY_KIND_SEND)
        {
            // save reply cap
            Pid caller_pid = (res.kind == WAITANY_KIND_CALL) ? res.w1 : res.source;
            uint32_t xlen = (res.kind == WAITANY_KIND_CALL) ? res.w2 : res.w1;
            NtRequest r;
            Message reply = (Message){.w0 = 0, .w1 = 0, .w2 = 0, .w3 = 0};

            if (NtUnpack(LmsgBuf(), xlen, &r) < 0)
            {
                if (res.kind == WAITANY_KIND_CALL)
                    ZuzuMsgReply(res.source, ERR_BADARG, 0, 0);
                continue;
            }
            // dispatch on opcode
            switch (r.cmd)
            {
            case NT_REGISTER:
                NtRegister(&r, res.label, caller_pid, &reply);
                break;
            case NT_LOOKUP:
                NtLookup(&r, res.label, caller_pid, &reply);
                break;
            case NT_LOOKUP_PID:
                NtLookupPid(&r, res.label, caller_pid, &reply);
                break;
            case NT_SCRUB_PID:
                NtScrubPid(&r, res.label, caller_pid, &reply);
                break;
            default:
                reply.w1 = ERR_BADARG;
                break;
            }
            if (res.kind == WAITANY_KIND_CALL)
            {
                ZuzuMsgReply(res.source, reply.w1, reply.w2, reply.w3);
            }
        }
    }
    return ZUZU_OK;
}
