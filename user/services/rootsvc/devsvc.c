#include "rootsvc.h"
#include <dev/protocols/devm.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

static Handle g_devsvc_port;

static int DevmUnpack(const char *buf, uint32_t xlen, DevmRequest *out)
{
    /* 1. Header must fit: cmd(4) + count(4). */
    if (xlen < 8)
    {
        return ERR_BADARG;
    }

    uint32_t cmd;
    uint32_t count;
    memcpy(&cmd, buf, 4); /* alignment- and aliasing-safe */
    memcpy(&count, buf + 4, 4);

    /* 2. Bound count before it indexes strings[]. */
    if (count == 0 || count > DEVM_MAX_COMPAT)
    {
        return ERR_BADARG;
    }

    /* 3. Bounded walk of `count` NUL-terminated strings. */
    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++)
    {
        if (off >= xlen)
        {
            return ERR_BADARG; /* ran out before string i */
        }

        uint32_t remaining = xlen - off;
        size_t len = strnlen(buf + off, remaining);
        if (len == remaining)
        {
            return ERR_BADARG; /* no NUL within bounds */
        }

        out->strings[i] = buf + off;
        off += (uint32_t)len + 1; /* +1 steps over the NUL */
    }

    if (off != xlen)
    {
        return ERR_BADARG;
    }

    out->cmd = (DevmRequestType)cmd;
    out->count = count;
    return ZUZU_OK;
}

void DevsvcMain(void)
{
    g_devsvc_port = CreatePort();
    if (g_devsvc_port < 0)
        return;
    HandleDuplicate(g_devsvc_port, PERM_MAP, 0xDE71CE00);
    RegisterServiceGeneric("/svc/devsvc",  g_devsvc_port, g_nsvc_port);

    for (;;)
    {
        PortWaitResult result = FormatToPortWait(WaitOn(g_devsvc_port, TIMEOUT_INFINITE));
        if (result.status != ZUZU_OK)
            continue;

        DevmRequest req;
        if (DevmUnpack(MessageBuf(), result.xlen, &req) != ZUZU_OK)
            continue;

        switch (req.cmd)
        {
        case DEVM_REQUEST:
        {
            uint32_t matched_index = 0;
            bool found = false;
            for (uint32_t i = 0; i < req.count && !found; i++)
            {
                for (uint32_t d = 0; d < g_bootinfo->dev_count; d++)
                {
                    if (strcmp(req.strings[i], g_bootinfo->devs[d].compatible) == 0)
                    {
                        matched_index = d;
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
                break;

            SvcResult dup =
                HandleDuplicate(DEVICE_HANDLE_BASE + matched_index, PERM_MAP, MARKER_NONE);
            if (dup.r0 != ZUZU_OK)
                break;

            memcpy(MessageBuf(), &matched_index, sizeof(matched_index));
            Reply(sizeof(matched_index), (Handle)dup.r1);
        }
        break;
        default:
            break;
        }
    }
}
