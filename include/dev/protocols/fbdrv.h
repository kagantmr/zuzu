/**
 * @file
 * @brief Driver side of the display stack.
 *
 * A display driver connects to gfxsvc ("/svc/gfx"), which owns the public API in fb.h. The driver
 * shares one control page with gfxsvc and hands over its framebuffer and an event.
 */

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

/** @brief Offer the control page to gfxsvc. Grants the page; replies FbDrvConnectReply and grants gfxsvc's doorbell. */
#define FBDRV_PROVIDE 0x101
/** @brief Hand over the framebuffer memory object. Grants it; FbDrvRequest. */
#define FBDRV_SETBUF 0x102
/** @brief Give gfxsvc the driver's event; arg is the bit to ring. Grants the event; FbDrvRequest. */
#define FBDRV_NOTIFY 0x103
/** @brief Tie the connection to the driver's life. Grants a port the driver owns; FbDrvRequest. */
#define FBDRV_WATCH 0x104

/** @brief Full memory barrier between writes to the control page and ringing the other side. */
#define FBDRV_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)

/**
 * @brief The page shared between a display driver and gfxsvc.
 *
 * The first six fields belong to the driver and are constant after FBDRV_PROVIDE. gfxsvc writes
 * the next three and then rings the driver, @c flip_seq last. The driver writes the last two and
 * then rings gfxsvc.
 */
typedef struct {
    uint32_t width;               /**< Width in pixels. */
    uint32_t height;              /**< Height in pixels. */
    uint32_t stride;              /**< Bytes from one row to the next. */
    uint32_t format;              /**< An FB_FMT_* value. */
    uint32_t buffers;             /**< Images in the framebuffer memory object. */
    uint32_t buf_size;            /**< Bytes per image, a whole number of pages. */
    volatile uint32_t flip_index; /**< Image to show next. */
    volatile uint32_t flip_seq;   /**< Bumped for each flip request, after @c flip_index. */
    volatile uint32_t vsync_on;   /**< Non-zero while vertical-sync events are wanted. */
    volatile uint32_t flip_ack;   /**< The @c flip_seq whose image is now on screen. */
    volatile uint32_t vsync_seq;  /**< Bumped by the driver for each frame while @c vsync_on is set. */
} FbDrvCtl;

_Static_assert(sizeof(FbDrvCtl) <= 4096, "FbDrvCtl must fit in the one shared page");

/** @brief A driver request to gfxsvc after FBDRV_PROVIDE. */
typedef struct {
    uint32_t cmd; /**< An FBDRV_* value. */
    uint32_t arg; /**< Command argument, such as the event bit for FBDRV_NOTIFY. */
} FbDrvRequest;

/** @brief gfxsvc's reply to FBDRV_PROVIDE. */
typedef struct {
    Err status;   /**< ZUZU_OK, or an error code. */
    uint32_t bit; /**< Bit to ring on gfxsvc's doorbell. */
} FbDrvConnectReply;

/** @brief A driver's connection to gfxsvc. */
typedef struct {
    FbDrvCtl *ctl;   /**< The mapped control page. */
    Handle ctl_mem;  /**< The control page's memory object. */
    Handle doorbell; /**< Send-only duplicate of gfxsvc's event. */
    uint32_t bit;    /**< Bit to ring on @c doorbell. */
    Handle live;     /**< Port gfxsvc watches to learn when the driver dies. */
} FbDrvConn;

/**
 * @brief Sends one request to gfxsvc and reads the status word of its reply.
 *
 * @param[in]  port    Handle to gfxsvc.
 * @param[in]  req     Request bytes.
 * @param[in]  len     Length of @p req.
 * @param[in]  grant   Handle to grant with the call, or -1.
 * @param[out] granted Receives a handle gfxsvc granted back; may be NULL.
 * @return gfxsvc's status, or the call's error.
 * @retval ERR_MALFORMED The reply was too short to hold a status.
 */
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

/**
 * @brief Sends a request that grants a duplicate of an object.
 *
 * @param[in] port  Handle to gfxsvc.
 * @param[in] cmd   An FBDRV_* value.
 * @param[in] arg   Command argument.
 * @param[in] obj   Handle to duplicate and grant.
 * @param[in] perms Permissions for the duplicate; PERM_TXFR is added.
 * @return gfxsvc's status, or the error from duplicating @p obj.
 */
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

/**
 * @brief Releases everything a connection holds and marks it unused.
 *
 * @param[in,out] c The connection.
 */
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

/**
 * @brief Runs the whole handshake with gfxsvc.
 *
 * @param[in]  gfx_port Handle to gfxsvc.
 * @param[in]  info     Supplies the six constant FbDrvCtl fields.
 * @param[in]  fb_mem   The framebuffer memory object.
 * @param[in]  my_event Event gfxsvc rings to wake the driver.
 * @param[in]  my_bit   Bit gfxsvc sets on @p my_event.
 * @param[out] c        The connection.
 * @return ZUZU_OK, or the first error; the connection is released on failure.
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
    Err rc = HandleDup(c->ctl_mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE, &page);
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
        rc = FbDrvGrantCall(gfx_port, FBDRV_SETBUF, 0, fb_mem, PERM_MAP | PERM_WRITE);
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
