#include "wm.h"

#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "wm"
#include <util/log.h>

#define CASCADE_STEP 32
#define CASCADE_SLOTS 6

Window g_win[WM_MAX_WINDOWS];
int g_stack[WM_MAX_WINDOWS];
int g_nstack;
int g_focus = -1;

static Client g_clients[WM_MAX_CLIENTS];
static Handle g_port = -1;
static uint32_t g_next_token = 1;
static uint32_t g_next_id = 1;
static uint32_t g_cascade;

static void CloseIf(Handle *h)
{
    if (*h >= 0)
        HandleClose(*h);
    *h = -1;
}

static void ReplyStatus(Err status)
{
    WmReply r = {.status = status};
    memcpy(GetMessageBox(), &r, sizeof(r));
    Reply(sizeof(r), -1);
}

static void ReplyWith(const WmReply *r, Handle grant)
{
    memcpy(GetMessageBox(), r, sizeof(*r));
    Reply(sizeof(*r), grant);
}

void WinSignal(int i, uint32_t bits)
{
    Window *w = &g_win[i];
    if (w->used && w->event >= 0)
        Signal(w->event, bits << w->shift, false);
}

void WinNotifyAll(uint32_t bits)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].layer != WM_LAYER_NORMAL)
            WinSignal(i, bits);
    }
}

static int StackIndex(int i)
{
    for (int s = 0; s < g_nstack; s++) {
        if (g_stack[s] == i)
            return s;
    }
    return -1;
}

static void StackRemove(int i)
{
    int s = StackIndex(i);
    if (s < 0)
        return;
    memmove(&g_stack[s], &g_stack[s + 1], (size_t)(g_nstack - s - 1) * sizeof(int));
    g_nstack--;
}

void WinRaise(int i)
{
    StackRemove(i);
    int s = g_nstack;
    while (s > 0 && g_win[g_stack[s - 1]].layer > g_win[i].layer)
        s--;
    memmove(&g_stack[s + 1], &g_stack[s], (size_t)(g_nstack - s) * sizeof(int));
    g_stack[s] = i;
    g_nstack++;
    Damage(WinOuter(&g_win[i]));
}

void WinFocus(int i)
{
    Window *w = &g_win[i];
    if (w->layer != WM_LAYER_NORMAL)
        return;
    if (w->minimized) {
        w->minimized = false;
        Damage(WinOuter(w));
    }
    WinRaise(i);
    if (g_focus != i) {
        if (g_focus >= 0 && g_win[g_focus].used)
            Damage(WinOuter(&g_win[g_focus]));
        g_focus = i;
    }
    WinNotifyAll(WM_EVT_WINDOWS);
}

void WinFocusTopmost(void)
{
    int old = g_focus;
    g_focus = -1;
    for (int s = g_nstack - 1; s >= 0; s--) {
        Window *w = &g_win[g_stack[s]];
        if (w->layer == WM_LAYER_NORMAL && !w->minimized) {
            g_focus = g_stack[s];
            Damage(WinOuter(w));
            break;
        }
    }
    if (old != g_focus)
        WinNotifyAll(WM_EVT_WINDOWS);
}

void WinMinimize(int i)
{
    Window *w = &g_win[i];
    if (w->layer != WM_LAYER_NORMAL || w->minimized)
        return;
    w->minimized = true;
    Damage(WinOuter(w));
    if (g_focus == i)
        WinFocusTopmost();
    WinNotifyAll(WM_EVT_WINDOWS);
}

void WinDestroy(int i)
{
    Window *w = &g_win[i];
    if (!w->used)
        return;
    Damage(WinOuter(w));
    InputWindowGone(i);
    StackRemove(i);
    if (w->px)
        MemUnmap((void *)w->px);
    if (w->ring)
        MemUnmap(w->ring);
    CloseIf(&w->mem);
    CloseIf(&w->event);
    CloseIf(&w->ring_mem);
    *w = (Window){.mem = -1, .event = -1, .ring_mem = -1};
    if (g_focus == i)
        WinFocusTopmost();
    WinNotifyAll(WM_EVT_WINDOWS);
}

int WinAt(int32_t x, int32_t y)
{
    for (int s = g_nstack - 1; s >= 0; s--) {
        const Window *w = &g_win[g_stack[s]];
        if (WinVisible(w) && RectContains(WinOuter(w), x, y))
            return g_stack[s];
    }
    return -1;
}

static void DropClient(int c)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].client == c)
            WinDestroy(i);
    }
    CloseIf(&g_clients[c].live);
    g_clients[c].token = 0;
}

void ServerReap(void)
{
    for (int c = 0; c < WM_MAX_CLIENTS; c++) {
        if (g_clients[c].live >= 0 && !HandleIsAlive(g_clients[c].live))
            DropClient(c);
    }
}

static int FindClient(uint32_t token)
{
    for (int c = 0; token && c < WM_MAX_CLIENTS; c++) {
        if (g_clients[c].live >= 0 && g_clients[c].token == token)
            return c;
    }
    return -1;
}

static int FindWindow(int c, uint32_t id)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].client == c && g_win[i].id == id)
            return i;
    }
    return -1;
}

static int FindAnyWindow(uint32_t id)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].id == id)
            return i;
    }
    return -1;
}

static void ServeConnect(Handle live)
{
    if (live < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    ServerReap();
    for (int c = 0; c < WM_MAX_CLIENTS; c++) {
        if (g_clients[c].live >= 0)
            continue;
        g_clients[c] = (Client){.live = live, .token = g_next_token++};
        if (g_next_token == 0)
            g_next_token = 1;
        WmReply r = {.status = ZUZU_OK,
                     .token = g_clients[c].token,
                     .screen_w = (uint32_t)g_screen_w,
                     .screen_h = (uint32_t)g_screen_h};
        ReplyWith(&r, -1);
        return;
    }
    HandleClose(live);
    ReplyStatus(ERR_BUSY);
}

static void Place(Window *w, const WmCreate *req)
{
    if (w->layer != WM_LAYER_NORMAL) {
        w->surf.x = req->x;
        w->surf.y = req->y;
        return;
    }
    int32_t step = (int32_t)(g_cascade++ % CASCADE_SLOTS) * CASCADE_STEP;
    int32_t x = 60 + step;
    int32_t y = WM_TITLE_H + 24 + step;
    if (x + w->surf.w > g_screen_w)
        x = g_screen_w - w->surf.w - WM_BORDER;
    if (y + w->surf.h > g_screen_h)
        y = g_screen_h - w->surf.h - WM_BORDER;
    w->surf.x = x < WM_BORDER ? WM_BORDER : x;
    w->surf.y = y < WM_TITLE_H + WM_BORDER ? WM_TITLE_H + WM_BORDER : y;
}

static void ServeCreate(int c, const WmCreate *req, Handle *mem)
{
    if (*mem < 0 || req->width == 0 || req->height == 0 || req->width > (uint32_t)g_screen_w ||
        req->height > (uint32_t)g_screen_h || req->layer > WM_LAYER_PANEL) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    size_t need = (size_t)req->width * req->height * sizeof(uint16_t);
    size_t have = 0;
    if (HandleQuerySize(*mem, &have) != ZUZU_OK || have < need) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    int i = -1;
    for (int k = 0; k < (int)WM_MAX_WINDOWS; k++) {
        if (!g_win[k].used) {
            i = k;
            break;
        }
    }
    if (i < 0) {
        ReplyStatus(ERR_BUSY);
        return;
    }
    const uint16_t *px = MemMap(*mem, 0, PROT_READ);
    if (PtrIsErr(px)) {
        ReplyStatus((Err)(intptr_t)px);
        return;
    }
    Window *w = &g_win[i];
    *w = (Window){.used = true,
                  .client = c,
                  .id = g_next_id++,
                  .layer = req->layer,
                  .surf = {0, 0, (int32_t)req->width, (int32_t)req->height},
                  .mem = *mem,
                  .px = px,
                  .event = -1,
                  .ring_mem = -1};
    *mem = -1;
    memcpy(w->title, req->title, WM_TITLE_MAX);
    w->title[WM_TITLE_MAX - 1] = '\0';
    Place(w, req);
    WinRaise(i);
    if (w->layer == WM_LAYER_NORMAL)
        WinFocus(i);
    WinNotifyAll(WM_EVT_WINDOWS);
    WmReply r = {.status = ZUZU_OK, .id = w->id};
    ReplyWith(&r, -1);
}

static void ServeInputOpen(Window *w)
{
    if (!w->ring) {
        Handle mem = CreateMem(1, 0);
        if (mem < 0) {
            ReplyStatus((Err)mem);
            return;
        }
        void *va = MemMap(mem, 0, PROT_RW);
        if (PtrIsErr(va)) {
            HandleClose(mem);
            ReplyStatus((Err)(intptr_t)va);
            return;
        }
        w->ring_mem = mem;
        w->ring = va;
        ShmRingInit(&w->ring->hdr, INPUT_RING_BYTES);
    }
    SvcResult dup = HandleDuplicate(w->ring_mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK) {
        ReplyStatus((Err)dup.r0);
        return;
    }
    WmReply r = {.status = ZUZU_OK};
    ReplyWith(&r, (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
}

static void ServeDamage(int i, const WmDamage *d)
{
    Window *w = &g_win[i];
    Rect r = RectIntersect((Rect){d->x, d->y, (int32_t)d->width, (int32_t)d->height},
                           (Rect){0, 0, w->surf.w, w->surf.h});
    if (!RectEmpty(r) && WinVisible(w)) {
        r.x += w->surf.x;
        r.y += w->surf.y;
        Damage(r);
        w->frame_due = true;
    } else {
        WinSignal(i, WM_EVT_FRAME);
    }
    ReplyStatus(ZUZU_OK);
}

static void ServeList(void)
{
    WmList list = {.status = ZUZU_OK};
    for (int s = 0; s < g_nstack && list.count < WM_MAX_WINDOWS; s++) {
        const Window *w = &g_win[g_stack[s]];
        WmWindowInfo *e = &list.win[list.count++];
        e->id = w->id;
        e->focused = g_stack[s] == g_focus;
        e->minimized = w->minimized;
        e->layer = (uint8_t)w->layer;
        strncpy(e->title, w->title, WM_LIST_TITLE_MAX - 1);
    }
    memcpy(GetMessageBox(), &list, sizeof(list));
    Reply(sizeof(list), -1);
}

static void ServeWindowCmd(int c, uint32_t cmd, const uint8_t *msg, uint32_t xlen, Handle *granted)
{
    WmRequest req = {0};
    memcpy(&req, msg, xlen < sizeof(req) ? xlen : sizeof(req));

    if (cmd == WM_CMD_LIST) {
        ServeList();
        return;
    }
    if (cmd == WM_CMD_BOOT_STATUS) {
        WmCreate t = {0};
        memcpy(&t, msg, xlen < sizeof(t) ? xlen : sizeof(t));
        t.title[WM_TITLE_MAX - 1] = '\0';
        SplashStatus(t.title);
        ReplyStatus(ZUZU_OK);
        return;
    }
    if (cmd == WM_CMD_BOOT_DONE) {
        SplashFinish();
        ReplyStatus(ZUZU_OK);
        return;
    }
    if (cmd == WM_CMD_FOCUS || cmd == WM_CMD_MINIMIZE) {
        int i = FindAnyWindow(req.id);
        if (i < 0) {
            ReplyStatus(ERR_NOENT);
            return;
        }
        if (cmd == WM_CMD_FOCUS)
            WinFocus(i);
        else
            WinMinimize(i);
        ReplyStatus(ZUZU_OK);
        return;
    }

    int i = FindWindow(c, req.id);
    if (i < 0) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    Window *w = &g_win[i];
    switch (cmd) {
    case WM_CMD_SET_EVENT:
        if (*granted < 0 || req.arg > 32U - WM_EVT_BITS) {
            ReplyStatus(ERR_BADARG);
            return;
        }
        CloseIf(&w->event);
        w->event = *granted;
        w->shift = req.arg;
        *granted = -1;
        ReplyStatus(ZUZU_OK);
        WinSignal(i, WM_EVT_FRAME | WM_EVT_WINDOWS);
        break;
    case WM_CMD_INPUT_OPEN:
        ServeInputOpen(w);
        break;
    case WM_CMD_DAMAGE: {
        WmDamage d = {0};
        memcpy(&d, msg, xlen < sizeof(d) ? xlen : sizeof(d));
        ServeDamage(i, &d);
        break;
    }
    case WM_CMD_DESTROY:
        WinDestroy(i);
        ReplyStatus(ZUZU_OK);
        break;
    case WM_CMD_SET_TITLE: {
        WmCreate t = {0};
        memcpy(&t, msg, xlen < sizeof(t) ? xlen : sizeof(t));
        memcpy(w->title, t.title, WM_TITLE_MAX);
        w->title[WM_TITLE_MAX - 1] = '\0';
        if (w->layer == WM_LAYER_NORMAL)
            Damage(WinTitleBar(w));
        WinNotifyAll(WM_EVT_WINDOWS);
        ReplyStatus(ZUZU_OK);
        break;
    }
    default:
        ReplyStatus(ERR_NOSYS);
        break;
    }
}

static void HandleRequest(const PortWaitResult *r)
{
    Handle granted = r->granted;
    uint8_t msg[sizeof(WmCreate)] = {0};
    uint32_t xlen = r->xlen < sizeof(msg) ? r->xlen : (uint32_t)sizeof(msg);
    memcpy(msg, GetMessageBox(), xlen);
    uint32_t cmd = 0;
    uint32_t token = 0;
    if (xlen >= 2 * sizeof(uint32_t)) {
        memcpy(&cmd, msg, sizeof(cmd));
        memcpy(&token, msg + sizeof(cmd), sizeof(token));
    }

    if (cmd == WM_CMD_CONNECT) {
        ServeConnect(granted);
        return;
    }
    int c = FindClient(token);
    if (c < 0) {
        ReplyStatus(ERR_BADARG);
    } else if (cmd == WM_CMD_CREATE) {
        WmCreate req;
        memcpy(&req, msg, sizeof(req));
        ServeCreate(c, &req, &granted);
    } else {
        ServeWindowCmd(c, cmd, msg, xlen, &granted);
    }
    if (granted >= 0)
        HandleClose(granted);
}

void ServerDrain(void)
{
    for (;;) {
        PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_POLL));
        if (r.status != ZUZU_OK)
            break;
        HandleRequest(&r);
    }
}

Err ServerInit(Handle event, uint32_t bit)
{
    for (int c = 0; c < WM_MAX_CLIENTS; c++)
        g_clients[c] = (Client){.live = -1};
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++)
        g_win[i] = (Window){.mem = -1, .event = -1, .ring_mem = -1};
    g_port = CreatePort();
    if (g_port < 0)
        return (Err)g_port;
    Err rc = Bind(EVENT_PORT, event, g_port, bit);
    if (rc != ZUZU_OK)
        return rc;
    rc = RegisterService("/svc/wm", g_port);
    if (rc == ZUZU_OK)
        LOG_INFO(LOG_TAG, "serving /svc/wm");
    return rc;
}
