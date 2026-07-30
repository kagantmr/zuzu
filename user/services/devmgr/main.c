#include "devmgr.h"
#include <string.h>
#include <zuzu/lmsg.h>
#include <zuzu/channel.h>
#include <zuzu/types.h>
#include <mem.h>

#define MAX_DRIVERS 64
#define MAX_HANDLE_SCAN 256

Handle port = -1;


static int DevmUnpack(const char *buf, uint32_t xlen, DevmRequest *out)
{
    /* 1. Header must fit: cmd(4) + count(4). */
    if (xlen < 8) {
        return ERR_BADARG;
    }

    uint32_t cmd;
    uint32_t count;
    memcpy(&cmd,   buf,     4);   /* alignment- and aliasing-safe */
    memcpy(&count, buf + 4, 4);

    /* 2. Bound count before it indexes strings[]. */
    if (count == 0 || count > DEVM_MAX_COMPAT) {
        return ERR_BADARG;
    }

    /* 3. Bounded walk of `count` NUL-terminated strings. */
    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (off >= xlen) {
            return ERR_BADARG;              /* ran out before string i */
        }

        uint32_t remaining = xlen - off;
        size_t   len       = strnlen(buf + off, remaining);
        if (len == remaining) {
            return ERR_BADARG;              /* no NUL within bounds */
        }

        out->strings[i] = buf + off;
        off += (uint32_t)len + 1;           /* +1 steps over the NUL */
    }


    if (off != xlen) {
        return ERR_BADARG;
    }

    out->cmd   = (DevmRequestType)cmd;
    out->count = count;
    return ZUZU_OK;   
}


static void HandleDevRequest(Handle reply_handle, Pid sender_pid,
                             const DevmRequest *req)
{
    char compat_buf[DEVM_COMPAT_MAX];   /* our own out-buffer; NOT the lmsg */

    for (uint32_t i = 0; i < req->count; i++) {
        for (Handle h = 1; h < MAX_HANDLE_SCAN; h++) {
            int rc = ZuzuDeviceQuery(h, compat_buf, sizeof(compat_buf));
            if (rc < 0) {
                continue;               /* not a device handle / empty slot */
            }
            /* rc >= 0 is the device's IRQ (query returns irq in r0). */

            if (strcmp(compat_buf, req->strings[i]) != 0) {
                continue;
            }

            /* Match. Grant the device cap to the requesting driver. */
            Handle granted = ZuzuGrant(h, sender_pid);
            if (granted < 0) {
                ZuzuMsgReply(reply_handle, granted, 0, 0);
                return;
            }

            ZuzuMsgReply(reply_handle, ZUZU_OK, (MsgWord)granted, (MsgWord)i);
            return;
        }
    }

    /* No driver string matched any device. */
    ZuzuMsgReply(reply_handle, ERR_NOENT, 0, 0);
}

int DevmgrSetup(void)
{
    //build_class_table();
    port = ZuzuPortCreate();
    if (port < 0)
    {
        return port;
    }

    Handle ntSlot = ZuzuGrant(port, NAMETABLE_PID);
    if (ntSlot < 0)
    {
        return ntSlot;
    }

    (void)ZuzuMsgSend(NT_PORT, NT_REGISTER, nt_pack(DEVMGR_NAME), (MsgWord)ntSlot);

    return ZUZU_OK;
}

int main(void)
{
    if (DevmgrSetup() != 0)
    {
        return ERR_SYSDOWN;
    }

    while (1)
    {
        Message msg = ZuzuMsgRecv(port, TIMEOUT_INFINITE);

        Handle   reply_handle = msg.w0;   // kernel-assigned reply-cap slot
        Pid      sender_pid   = msg.w1;   // kernel-stamped, trustworthy
        size_t xlen         = msg.w2;   // buffer length = your bounds ceiling

        DevmRequest req;
        if (DevmUnpack(LmsgBuf(), xlen, &req) != 0) {
            ZuzuMsgReply(reply_handle, ERR_BADARG, 0, 0);
            continue;
        }
        switch (req.cmd) {
            case DEVM_REQUEST:
                HandleDevRequest(reply_handle, sender_pid, &req);
                break;
            default:
                ZuzuMsgReply(reply_handle, ERR_NOSYS, 0, 0);
        }
    }

    return 0;
}