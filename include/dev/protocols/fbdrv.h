#ifndef FBDRV_PROTOCOL_H
#define FBDRV_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/msg.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

/*
 * Driver side of the display stack. A display driver connects to gfxsvc
 * ("/svc/gfx"), which owns the public API in fb.h.
 */

#define FBDRV_PROVIDE 0x101
#define FBDRV_SETBUF 0x102
#define FBDRV_NOTIFY 0x103
#define FBDRV_WATCH 0x104

#define FBDRV_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)

typedef struct {
    /* driver-owned, constant after FBDRV_PROVIDE */
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t buffers;
    uint32_t buf_size;
    /* gfxsvc writes, then rings the driver: flip_seq last */
    volatile uint32_t flip_index;
    volatile uint32_t flip_seq;
    volatile uint32_t vsync_on;
    /* driver writes, then rings gfxsvc */
    volatile uint32_t flip_ack; /* the flip_seq whose image is now on screen */
    volatile uint32_t vsync_seq;
} FbDrvCtl;

_Static_assert(sizeof(FbDrvCtl) <= 4096, "FbDrvCtl must fit in the one shared page");

typedef struct {
    uint32_t cmd;
    uint32_t arg;
} FbDrvRequest;

typedef struct {
    Err status;
    uint32_t bit; /* bit to ring on gfxsvc's doorbell */
} FbDrvConnectReply;

typedef struct {
    FbDrvCtl *ctl;
    Handle ctl_mem;
    Handle doorbell; /* send-only dup of gfxsvc's event */
    uint32_t bit;
    Handle live;
} FbDrvConn;

static inline Err FbDrvCall(Handle port, const void *req, uint32_t len, Handle grant,
                            Handle *granted)
{
    MsgWrite(req, len);
    uint32_t got = 0;
    Handle back = -1;
    Err rc = CallPort(port, len, grant, &got, &back);
    if (granted)
        *granted = back;
    if (rc != ZUZU_OK)
        return rc;
    Err status;
    if (got < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    return status;
}

static inline Err FbDrvGrantCall(Handle port, uint32_t cmd, uint32_t arg, Handle obj,
                                 HandlePerms perms)
{
    Handle dup;
    Err rc = HandleDup(obj, perms | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    FbDrvRequest req = {.cmd = cmd, .arg = arg};
    rc = FbDrvCall(port, &req, sizeof(req), dup, NULL);
    HandleClose(dup);
    return rc;
}

static inline void FbDrvDisconnect(FbDrvConn *c)
{
    if (c->ctl)
        MemUnmap(c->ctl);
    if (c->ctl_mem >= 0)
        HandleClose(c->ctl_mem);
    if (c->doorbell >= 0)
        HandleClose(c->doorbell);
    if (c->live >= 0)
        HandleClose(c->live);
    c->ctl = NULL;
    c->ctl_mem = c->doorbell = c->live = -1;
}

/*
 * Runs the whole handshake. `info` supplies the six constant FbDrvCtl fields;
 * `fb_mem` is the framebuffer MemObject and `my_event`/`my_bit` is how gfxsvc
 * wakes the driver.
 */
static inline Err FbDrvConnect(Handle gfx_port, const FbDrvCtl *info, Handle fb_mem,
                               Handle my_event, uint32_t my_bit, FbDrvConn *c)
{
    c->ctl = NULL;
    c->ctl_mem = c->doorbell = c->live = -1;

    c->ctl_mem = CreateMem(1, 0);
    if (c->ctl_mem < 0)
        return (Err)c->ctl_mem;
    void *va = MemMap(c->ctl_mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        Err rc = (Err)va;
        FbDrvDisconnect(c);
        return rc;
    }
    c->ctl = va;
    memset(c->ctl, 0, sizeof(*c->ctl));
    c->ctl->width = info->width;
    c->ctl->height = info->height;
    c->ctl->stride = info->stride;
    c->ctl->format = info->format;
    c->ctl->buffers = info->buffers;
    c->ctl->buf_size = info->buf_size;

    Handle page;
    Err rc = HandleDup(c->ctl_mem, PERM_MAP | PERM_TXFR, MARKER_NONE, &page);
    if (rc == ZUZU_OK) {
        FbDrvRequest req = {.cmd = FBDRV_PROVIDE};
        Handle bell = -1;
        rc = FbDrvCall(gfx_port, &req, sizeof(req), page, &bell);
        HandleClose(page);
        if (rc == ZUZU_OK && bell < 0)
            rc = ERR_MALFORMED;
        if (rc == ZUZU_OK) {
            FbDrvConnectReply rep;
            memcpy(&rep, GetMessageBox(), sizeof(rep));
            c->doorbell = bell;
            c->bit = rep.bit;
        } else if (bell >= 0) {
            HandleClose(bell);
        }
    }

    if (rc == ZUZU_OK)
        rc = FbDrvGrantCall(gfx_port, FBDRV_SETBUF, 0, fb_mem, PERM_MAP);
    if (rc == ZUZU_OK)
        rc = FbDrvGrantCall(gfx_port, FBDRV_NOTIFY, my_bit, my_event, PERM_SEND);
    if (rc == ZUZU_OK) {
        c->live = CreatePort();
        if (c->live < 0)
            rc = (Err)c->live;
        else
            rc = FbDrvGrantCall(gfx_port, FBDRV_WATCH, 0, c->live, PERM_WAIT);
    }
    if (rc != ZUZU_OK)
        FbDrvDisconnect(c);
    return rc;
}

#ifdef __cplusplus
}
#endif

#endif /* FBDRV_PROTOCOL_H */
