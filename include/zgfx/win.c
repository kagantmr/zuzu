/**
 * @file
 * @brief Client side of the window manager protocol.
 */

#include <string.h>
#include <zgfx/win.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/** @brief Delay between /svc/wm lookups while waiting for wm to start. */
#define LOOKUP_STEP_MS 10
/** @brief Page size surfaces and input rings are rounded up to. */
#define PAGE_BYTES 4096U

/**
 * @brief Sends one request to wm and copies its reply.
 *
 * @param[in]  conn        Connection.
 * @param[in]  req         Request.
 * @param[in]  len         Request length.
 * @param[in]  grant       Handle to grant, or -1.
 * @param[out] reply       Reply buffer, or NULL.
 * @param[in]  reply_len   Reply length to copy.
 * @param[out] reply_grant Granted handle, or NULL.
 * @return wm's status or an IPC error.
 */
static Err WmCall(WinConn *conn, const void *req, uint32_t len, Handle grant, void *reply,
                  uint32_t reply_len, Handle *reply_grant)
{
    MsgWrite(req, len);
    uint32_t got = 0;
    Err rc = CallPort(conn->port, len, grant, &got, reply_grant);
    if (rc != ZUZU_OK)
        return rc;
    if (got < sizeof(Err))
        return ERR_MALFORMED;
    Err status;
    memcpy(&status, GetMessageBox(), sizeof(status));
    if (reply && got >= reply_len)
        memcpy(reply, GetMessageBox(), reply_len);
    return status;
}

/**
 * @brief Sends a WmRequest that has no reply payload.
 *
 * @param[in] conn  Connection.
 * @param[in] cmd   Command.
 * @param[in] id    Window id.
 * @param[in] arg   Argument.
 * @param[in] grant Handle to grant, or -1.
 * @return wm's status or an IPC error.
 */
static Err Simple(WinConn *conn, uint32_t cmd, uint32_t id, uint32_t arg, Handle grant)
{
    WmRequest req = {.cmd = cmd, .token = conn->token, .id = id, .arg = arg};
    return WmCall(conn, &req, sizeof(req), grant, NULL, 0, NULL);
}

Err WinConnect(WinConn *conn, uint32_t wait_ms)
{
    *conn = (WinConn){.port = -1, .live = -1};
    for (uint32_t waited = 0;; waited += LOOKUP_STEP_MS) {
        conn->port = LookupService("/svc/wm");
        if (conn->port >= 0)
            break;
        if (waited >= wait_ms)
            return ERR_NOENT;
        Sleep(LOOKUP_STEP_MS);
    }
    conn->live = CreatePort();
    if (conn->live < 0)
        return (Err)conn->live;
    Handle dup;
    Err rc = HandleDup(conn->live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    WmRequest req = {.cmd = WM_CMD_CONNECT};
    WmReply rep = {0};
    rc = WmCall(conn, &req, sizeof(req), dup, &rep, sizeof(rep), NULL);
    HandleClose(dup);
    if (rc != ZUZU_OK)
        return rc;
    conn->token = rep.token;
    conn->screen_w = rep.screen_w;
    conn->screen_h = rep.screen_h;
    return ZUZU_OK;
}

Err WinCreate(WinConn *conn, const WinSpec *spec, Handle event, uint32_t shift, Win *win)
{
    *win = (Win){
        .conn = conn, .width = spec->width, .height = spec->height, .mem = -1, .ring_mem = -1};
    size_t bytes = (size_t)spec->width * spec->height * sizeof(uint16_t);
    win->mem = CreateMem((bytes + PAGE_BYTES - 1U) / PAGE_BYTES, 0);
    if (win->mem < 0)
        return (Err)win->mem;
    win->px = MemMap(win->mem, 0, PROT_RW);
    if (PtrIsErr(win->px)) {
        Err rc = (Err)(intptr_t)win->px;
        win->px = NULL;
        WinClose(win);
        return rc;
    }
    memset(win->px, 0, bytes);

    Handle dup;
    Err rc = HandleDup(win->mem, PERM_MAP | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK) {
        WinClose(win);
        return rc;
    }
    WmCreate req = {.cmd = WM_CMD_CREATE,
                    .token = conn->token,
                    .layer = spec->layer,
                    .x = spec->x,
                    .y = spec->y,
                    .width = spec->width,
                    .height = spec->height};
    if (spec->title)
        strncpy(req.title, spec->title, WM_TITLE_MAX - 1);
    WmReply rep = {0};
    rc = WmCall(conn, &req, sizeof(req), dup, &rep, sizeof(rep), NULL);
    HandleClose(dup);
    if (rc != ZUZU_OK) {
        WinClose(win);
        return rc;
    }
    win->id = rep.id;

    Handle ring = -1;
    WmRequest open = {.cmd = WM_CMD_INPUT_OPEN, .token = conn->token, .id = win->id};
    rc = WmCall(conn, &open, sizeof(open), -1, NULL, 0, &ring);
    if (rc == ZUZU_OK && ring >= 0) {
        InputRing *va = MemMap(ring, 0, PROT_RW);
        if (!PtrIsErr(va)) {
            win->ring = va;
            win->ring_mem = ring;
        } else {
            HandleClose(ring);
        }
    }

    rc = HandleDup(event, PERM_SEND | PERM_TXFR, MARKER_NONE, &dup);
    if (rc == ZUZU_OK) {
        rc = Simple(conn, WM_CMD_SET_EVENT, win->id, shift, dup);
        HandleClose(dup);
    }
    if (rc != ZUZU_OK)
        WinClose(win);
    return rc;
}

Err WinDamage(Win *win, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    WmDamage req = {.cmd = WM_CMD_DAMAGE,
                    .token = win->conn->token,
                    .id = win->id,
                    .x = x,
                    .y = y,
                    .width = width,
                    .height = height};
    return WmCall(win->conn, &req, sizeof(req), -1, NULL, 0, NULL);
}

Err WinSetTitle(Win *win, const char *title)
{
    WmCreate req = {.cmd = WM_CMD_SET_TITLE, .token = win->conn->token, .id = win->id};
    strncpy(req.title, title, WM_TITLE_MAX - 1);
    return WmCall(win->conn, &req, sizeof(req), -1, NULL, 0, NULL);
}

void WinClose(Win *win)
{
    if (win->id && win->conn)
        Simple(win->conn, WM_CMD_DESTROY, win->id, 0, -1);
    if (win->ring)
        MemUnmap(win->ring);
    if (win->ring_mem >= 0)
        HandleClose(win->ring_mem);
    if (win->px)
        MemUnmap(win->px);
    if (win->mem >= 0)
        HandleClose(win->mem);
    *win = (Win){.mem = -1, .ring_mem = -1};
}

uint32_t WinReadInput(Win *win, InputEvent *out, uint32_t max)
{
    return win->ring ? InputRingRead(win->ring, out, max) : 0;
}

Err WinList(WinConn *conn, WmList *out)
{
    WmRequest req = {.cmd = WM_CMD_LIST, .token = conn->token};
    return WmCall(conn, &req, sizeof(req), -1, out, sizeof(*out), NULL);
}

Err WinFocusId(WinConn *conn, uint32_t id) { return Simple(conn, WM_CMD_FOCUS, id, 0, -1); }

Err WinMinimizeId(WinConn *conn, uint32_t id) { return Simple(conn, WM_CMD_MINIMIZE, id, 0, -1); }

Err WinBootStatus(WinConn *conn, const char *text)
{
    WmCreate req = {.cmd = WM_CMD_BOOT_STATUS, .token = conn->token};
    strncpy(req.title, text, WM_TITLE_MAX - 1);
    return WmCall(conn, &req, sizeof(req), -1, NULL, 0, NULL);
}

Err WinBootDone(WinConn *conn) { return Simple(conn, WM_CMD_BOOT_DONE, 0, 0, -1); }
