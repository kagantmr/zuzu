#include <dev/protocols/fb.h>
#include <dev/hid.h>
#include <dev/protocols/fbdrv.h>
#include <dev/protocols/input.h>
#include <stddef.h>
#include <string.h>
#include <types.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "gfxsvc"
#include <util/log.h>

#define BIT_PORT 0
#define BIT_DRIVER 1
#define MASK(bit) (1U << (bit))
#define POLL_MS 10
#define FB_MAX_IMAGES 4U
#define NO_VT (-1)
#define NO_IMAGE UINT32_MAX
#define MOD_CTRL_L (1U << 0)
#define MOD_CTRL_R (1U << 1)
#define MOD_ALT_L (1U << 2)
#define MOD_ALT_R (1U << 3)

typedef struct {
    bool ready;
    FbDrvCtl *ctl;
    Handle ctl_mem;
    Handle fb_mem;
    Handle bell;
    uint32_t bell_bit;
    Handle live;
    uint32_t flip_sent;
    uint32_t vsync_seen;
    bool flip_pending;
    bool flip_quiet;
    uint32_t flip_target;
    uint32_t shown;
    uint32_t want;
} Display;

typedef struct {
    Handle live;
    Handle event;
    uint32_t shift;
    uint32_t token;
    uint32_t image;
    Handle ring_mem;
    InputRing *ring;
} Client;

typedef struct {
    bool placed;
    bool moved;
    int32_t x;
    int32_t y;
    uint32_t mods;
    bool delivered;
} Pointer;

static Handle g_port = -1;
static Handle g_event = -1;
static Display g_disp = {.ctl_mem = -1, .fb_mem = -1, .bell = -1, .live = -1, .want = NO_IMAGE};
static Client g_clients[FB_MAX_VTS];
static int g_active = NO_VT;
static int g_switch_to = NO_VT;
static uint64_t g_switch_deadline;
static uint32_t g_next_token = 1;
static Pointer g_input;

static void ReplyStatus(Err status)
{
    memcpy(GetMessageBox(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void CloseIf(Handle *h)
{
    if (*h >= 0)
        HandleClose(*h);
    *h = -1;
}

static void KickDriver(void)
{
    if (g_disp.bell >= 0)
        Signal(g_disp.bell, MASK(g_disp.bell_bit), false);
}

static bool ClientUsed(int vt) { return vt >= 0 && g_clients[vt].live >= 0; }

static void ClientSignal(int vt, uint32_t bits)
{
    if (ClientUsed(vt) && g_clients[vt].event >= 0)
        Signal(g_clients[vt].event, bits << g_clients[vt].shift, false);
}

static void SetVsync(bool on)
{
    if (!g_disp.ctl)
        return;
    g_disp.ctl->vsync_on = on ? 1U : 0U;
    FBDRV_FENCE();
    KickDriver();
}

static void DoFlip(uint32_t index, bool quiet)
{
    g_disp.ctl->flip_index = index;
    FBDRV_FENCE();
    g_disp.ctl->flip_seq = ++g_disp.flip_sent;
    g_disp.flip_pending = true;
    g_disp.flip_quiet = quiet;
    g_disp.flip_target = index;
    KickDriver();
}

static void ShowImage(uint32_t index)
{
    if (!g_disp.ready)
        return;
    if (g_disp.flip_pending) {
        g_disp.flip_quiet = true;
        g_disp.want = index;
        return;
    }
    g_disp.want = NO_IMAGE;
    if (index != g_disp.shown)
        DoFlip(index, true);
}

static void SendPointer(void);

static void Activate(int vt)
{
    g_active = vt;
    ShowImage(g_clients[vt].image);
    SendPointer();
    ClientSignal(vt, FB_EVT_ACTIVATE | (g_input.delivered ? FB_EVT_INPUT : 0U));
    g_input.delivered = false;
    LOG_INFO(LOG_TAG, "vt%d active", vt + 1);
}

static void ActivateNext(int from)
{
    g_active = NO_VT;
    for (uint32_t k = 1; k <= FB_MAX_VTS; k++) {
        int vt = (int)(((uint32_t)from + k) % FB_MAX_VTS);
        if (ClientUsed(vt)) {
            Activate(vt);
            return;
        }
    }
}

static void FinishSwitch(void)
{
    int old = g_active;
    int to = g_switch_to;
    g_switch_to = NO_VT;
    SetVsync(false);
    if (ClientUsed(to))
        Activate(to);
    else if (ClientUsed(old))
        Activate(old);
    else
        ActivateNext(old >= 0 ? old : 0);
}

static void DropClientHandles(Client *c)
{
    CloseIf(&c->live);
    CloseIf(&c->event);
    if (c->ring)
        MemUnmap(c->ring);
    CloseIf(&c->ring_mem);
    *c = (Client){.live = -1, .event = -1, .ring_mem = -1};
}

static void DropClient(int vt)
{
    DropClientHandles(&g_clients[vt]);

    if (vt == g_switch_to) {
        g_switch_to = NO_VT;
        if (ClientUsed(g_active))
            ClientSignal(g_active, FB_EVT_ACTIVATE);
    }
    if (vt == g_active) {
        if (g_switch_to != NO_VT) {
            FinishSwitch();
        } else {
            SetVsync(false);
            ActivateNext(vt);
        }
    }
}

static void ReapClients(void)
{
    for (int vt = 0; vt < (int)FB_MAX_VTS; vt++) {
        if (ClientUsed(vt) && !HandleIsAlive(g_clients[vt].live))
            DropClient(vt);
    }
}

static int FindClient(uint32_t token)
{
    if (token == 0)
        return NO_VT;
    for (int vt = 0; vt < (int)FB_MAX_VTS; vt++) {
        if (ClientUsed(vt) && g_clients[vt].token == token)
            return vt;
    }
    return NO_VT;
}

static void DropDisplay(void)
{
    for (int vt = 0; vt < (int)FB_MAX_VTS; vt++) {
        if (ClientUsed(vt))
            DropClientHandles(&g_clients[vt]);
    }
    g_active = g_switch_to = NO_VT;
    if (g_disp.ctl)
        MemUnmap(g_disp.ctl);
    CloseIf(&g_disp.ctl_mem);
    CloseIf(&g_disp.fb_mem);
    CloseIf(&g_disp.bell);
    CloseIf(&g_disp.live);
    g_disp = (Display){.ctl_mem = -1, .fb_mem = -1, .bell = -1, .live = -1, .want = NO_IMAGE};
}

static void FillInfo(FbInfo *info, Err status, uint32_t token)
{
    const FbDrvCtl *c = g_disp.ctl;
    *info = (FbInfo){
        .status = status,
        .width = c->width,
        .height = c->height,
        .stride = c->stride,
        .format = c->format,
        .buffers = c->buffers,
        .buf_size = c->buf_size,
        .token = token,
    };
}

static void ServeProvide(Handle page)
{
    if (page < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    if (g_disp.ctl_mem >= 0 && g_disp.live >= 0 && !HandleIsAlive(g_disp.live))
        DropDisplay();
    if (g_disp.ctl_mem >= 0) {
        HandleClose(page);
        ReplyStatus(ERR_BUSY);
        return;
    }

    void *va = MemMap(page, 0, PROT_RW);
    if (PtrIsErr(va)) {
        HandleClose(page);
        ReplyStatus((Err)va);
        return;
    }
    const FbDrvCtl *c = va;
    bool sane = c->buffers >= 1 && c->buffers <= FB_MAX_IMAGES && c->buf_size > 0 &&
                c->buf_size % 4096U == 0 && c->width > 0 && c->height > 0 &&
                c->stride * c->height <= c->buf_size;
    SvcResult bell = {.r0 = sane ? ZUZU_OK : ERR_BADARG};
    if (sane)
        bell = HandleDuplicate(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (bell.r0 != ZUZU_OK) {
        MemUnmap(va);
        HandleClose(page);
        ReplyStatus((Err)bell.r0);
        return;
    }

    g_disp.ctl = va;
    g_disp.ctl_mem = page;

    FbDrvConnectReply rep = {.status = ZUZU_OK, .bit = BIT_DRIVER};
    memcpy(GetMessageBox(), &rep, sizeof(rep));
    Reply(sizeof(rep), (Handle)bell.r1);
    HandleClose((Handle)bell.r1);
}

static void ServeDriverGrant(uint32_t cmd, uint32_t arg, Handle h)
{
    if (h < 0 || g_disp.ctl_mem < 0 || g_disp.ready) {
        if (h >= 0)
            HandleClose(h);
        ReplyStatus(h < 0 ? ERR_BADARG : ERR_NOTCONN);
        return;
    }

    if (cmd == FBDRV_SETBUF && g_disp.fb_mem < 0) {
        size_t size = 0;
        if (HandleQuerySize(h, &size) != ZUZU_OK ||
            size < (size_t)g_disp.ctl->buf_size * g_disp.ctl->buffers) {
            HandleClose(h);
            ReplyStatus(ERR_BADARG);
            return;
        }
        g_disp.fb_mem = h;
    } else if (cmd == FBDRV_NOTIFY && g_disp.bell < 0 && arg < 32U) {
        g_disp.bell = h;
        g_disp.bell_bit = arg;
    } else if (cmd == FBDRV_WATCH && g_disp.live < 0) {
        g_disp.live = h;
        g_disp.ready = g_disp.fb_mem >= 0 && g_disp.bell >= 0;
        if (g_disp.ready)
            LOG_INFO(LOG_TAG, "display up: %ux%u, %u image(s)", g_disp.ctl->width,
                     g_disp.ctl->height, g_disp.ctl->buffers);
    } else {
        HandleClose(h);
        ReplyStatus(ERR_DUPLICATE);
        return;
    }
    ReplyStatus(ZUZU_OK);
}

static void ServeGetInfo(void)
{
    if (!g_disp.ready) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    FbInfo info;
    FillInfo(&info, ZUZU_OK, 0);
    info.vt = (uint32_t)(g_active + 1);
    memcpy(GetMessageBox(), &info, sizeof(info));
    Reply(sizeof(info), -1);
}

static void ServeGetBuf(Handle live)
{
    if (!g_disp.ready) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    if (live < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    ReapClients();
    int vt = NO_VT;
    for (int i = 0; i < (int)FB_MAX_VTS; i++) {
        if (!ClientUsed(i)) {
            vt = i;
            break;
        }
    }
    if (vt == NO_VT) {
        HandleClose(live);
        ReplyStatus(ERR_BUSY);
        return;
    }
    SvcResult dup = HandleDuplicate(g_disp.fb_mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK) {
        HandleClose(live);
        ReplyStatus((Err)dup.r0);
        return;
    }
    Client *c = &g_clients[vt];
    *c = (Client){.live = live, .event = -1, .token = g_next_token++, .image = 0, .ring_mem = -1};
    if (g_next_token == 0)
        g_next_token = 1;
    if (g_active == NO_VT && g_switch_to == NO_VT)
        Activate(vt);

    FbInfo info;
    FillInfo(&info, ZUZU_OK, c->token);
    info.vt = (uint32_t)(vt + 1);
    info.active = vt == g_active ? 1U : 0U;
    memcpy(GetMessageBox(), &info, sizeof(info));
    Reply(sizeof(info), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
}

static Err StartSwitch(uint32_t target)
{
    if (!g_disp.ready)
        return ERR_NOENT;
    ReapClients();
    int vt = (int)target - 1;
    if (target == 0 || target > FB_MAX_VTS || !ClientUsed(vt))
        return ERR_NOENT;
    if (g_switch_to != NO_VT)
        return ERR_BUSY;
    if (vt == g_active)
        return ZUZU_OK;
    if (g_active == NO_VT) {
        Activate(vt);
    } else {
        g_switch_to = vt;
        g_switch_deadline = UptimeMs() + FB_SWITCH_TIMEOUT_MS;
        ClientSignal(g_active, FB_EVT_DEACTIVATE);
    }
    return ZUZU_OK;
}

static void Deliver(uint16_t type, uint16_t code, int32_t value, uint32_t time_ms)
{
    if (g_switch_to != NO_VT || !ClientUsed(g_active) || !g_clients[g_active].ring)
        return;
    InputEvent ev = {.type = type, .code = code, .value = value, .time_ms = time_ms};
    if (InputRingWrite(g_clients[g_active].ring, &ev))
        g_input.delivered = true;
}

static void PlacePointer(void)
{
    if (g_input.placed || !g_disp.ready)
        return;
    g_input.x = (int32_t)(g_disp.ctl->width / 2U);
    g_input.y = (int32_t)(g_disp.ctl->height / 2U);
    g_input.placed = true;
}

static void SendPointer(void)
{
    PlacePointer();
    uint32_t now = (uint32_t)UptimeMs();
    Deliver(INPUT_EV_ABS, INPUT_ABS_X, g_input.x, now);
    Deliver(INPUT_EV_ABS, INPUT_ABS_Y, g_input.y, now);
    Deliver(INPUT_EV_SYN, 0, 0, now);
}

static int32_t ClampAxis(int32_t v, uint32_t size)
{
    if (v < 0)
        return 0;
    if (v >= (int32_t)size)
        return (int32_t)size - 1;
    return v;
}

static uint32_t ModBit(uint16_t usage)
{
    switch (usage) {
    case HID_KEY_LCTRL:
        return MOD_CTRL_L;
    case HID_KEY_RCTRL:
        return MOD_CTRL_R;
    case HID_KEY_LALT:
        return MOD_ALT_L;
    case HID_KEY_RALT:
        return MOD_ALT_R;
    default:
        return 0;
    }
}

static bool IsHotkey(const InputEvent *ev)
{
    bool ctrl = (g_input.mods & (MOD_CTRL_L | MOD_CTRL_R)) != 0;
    bool alt = (g_input.mods & (MOD_ALT_L | MOD_ALT_R)) != 0;
    return ctrl && alt && ev->code >= HID_KEY_F1 && ev->code < HID_KEY_F1 + FB_MAX_VTS;
}

static void RouteEvent(const InputEvent *ev)
{
    switch (ev->type) {
    case INPUT_EV_KEY: {
        uint32_t bit = ModBit(ev->code);
        if (bit)
            g_input.mods = ev->value ? (g_input.mods | bit) : (g_input.mods & ~bit);
        if (IsHotkey(ev)) {
            if (ev->value)
                StartSwitch((uint32_t)(ev->code - HID_KEY_F1) + 1U);
            return;
        }
        Deliver(ev->type, ev->code, ev->value, ev->time_ms);
        break;
    }
    case INPUT_EV_REL:
        PlacePointer();
        if (ev->code == INPUT_REL_X)
            g_input.x = ClampAxis(g_input.x + ev->value, g_disp.ctl->width);
        else if (ev->code == INPUT_REL_Y)
            g_input.y = ClampAxis(g_input.y + ev->value, g_disp.ctl->height);
        g_input.moved = true;
        break;
    case INPUT_EV_BTN:
        Deliver(ev->type, ev->code, ev->value, ev->time_ms);
        break;
    case INPUT_EV_SYN:
        if (g_input.moved) {
            Deliver(INPUT_EV_ABS, INPUT_ABS_X, g_input.x, ev->time_ms);
            Deliver(INPUT_EV_ABS, INPUT_ABS_Y, g_input.y, ev->time_ms);
            g_input.moved = false;
        }
        Deliver(INPUT_EV_SYN, 0, 0, ev->time_ms);
        break;
    default:
        break;
    }
}

static void ServeInputPush(uint32_t xlen)
{
    InputPush push;
    uint32_t len = xlen < sizeof(push) ? xlen : (uint32_t)sizeof(push);
    memcpy(&push, GetMessageBox(), len);
    uint32_t room = len < offsetof(InputPush, ev) ? 0U
                                                  : (len - (uint32_t)offsetof(InputPush, ev)) /
                                                        (uint32_t)sizeof(InputEvent);
    if (!g_disp.ready || push.count > room) {
        ReplyStatus(g_disp.ready ? ERR_BADARG : ERR_NOENT);
        return;
    }
    ReplyStatus(ZUZU_OK);
    for (uint32_t i = 0; i < push.count; i++)
        RouteEvent(&push.ev[i]);
    if (g_input.delivered) {
        g_input.delivered = false;
        ClientSignal(g_active, FB_EVT_INPUT);
    }
}

static void ServeInputOpen(int vt)
{
    Client *c = &g_clients[vt];
    if (!c->ring) {
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
        c->ring_mem = mem;
        c->ring = va;
        ShmRingInit(&c->ring->hdr, INPUT_RING_BYTES);
    }
    SvcResult dup = HandleDuplicate(c->ring_mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK) {
        ReplyStatus((Err)dup.r0);
        return;
    }
    Err ok = ZUZU_OK;
    memcpy(GetMessageBox(), &ok, sizeof(ok));
    Reply(sizeof(ok), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (vt == g_active && g_switch_to == NO_VT) {
        SendPointer();
        g_input.delivered = false;
        ClientSignal(vt, FB_EVT_INPUT);
    }
}

static void ServeFlip(int vt, uint32_t index)
{
    if (vt != g_active || g_switch_to != NO_VT) {
        ReplyStatus(ERR_BUSY);
        return;
    }
    if (index >= g_disp.ctl->buffers) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    if (g_disp.flip_pending) {
        ReplyStatus(ERR_BUSY);
        return;
    }
    g_clients[vt].image = index;
    DoFlip(index, false);
    ReplyStatus(ZUZU_OK);
}

static void ServeClientCmd(const FbRequest *req, Handle *granted)
{
    ReapClients();
    int vt = FindClient(req->token);
    if (!g_disp.ready || vt == NO_VT) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    switch (req->cmd) {
    case FB_CMD_RELEASE:
        DropClient(vt);
        ReplyStatus(ZUZU_OK);
        break;
    case FB_CMD_SET_EVENT:
        if (*granted < 0 || req->arg >= 32U) {
            ReplyStatus(ERR_BADARG);
            break;
        }
        CloseIf(&g_clients[vt].event);
        g_clients[vt].event = *granted;
        g_clients[vt].shift = req->arg;
        *granted = -1;
        if (vt == g_active && g_switch_to == NO_VT)
            ClientSignal(vt, FB_EVT_ACTIVATE);
        ReplyStatus(ZUZU_OK);
        break;
    case FB_CMD_FLIP:
        ServeFlip(vt, req->arg);
        break;
    case FB_CMD_SET_VSYNC:
        if (vt != g_active || g_switch_to != NO_VT) {
            ReplyStatus(ERR_BUSY);
            break;
        }
        SetVsync(req->arg != 0);
        ReplyStatus(ZUZU_OK);
        break;
    case FB_CMD_DEACTIVATED:
        if (vt == g_active && g_switch_to != NO_VT)
            FinishSwitch();
        ReplyStatus(ZUZU_OK);
        break;
    case FB_CMD_INPUT_OPEN:
        ServeInputOpen(vt);
        break;
    default:
        ReplyStatus(ERR_NOSYS);
        break;
    }
}

static void HandleRequest(const PortWaitResult *r)
{
    Handle granted = r->granted;
    FbRequest req = {0};

    if (r->xlen < sizeof(uint32_t)) {
        ReplyStatus(ERR_BADARG);
        goto out;
    }
    memcpy(&req, GetMessageBox(), r->xlen < sizeof(req) ? r->xlen : sizeof(req));

    switch (req.cmd) {
    case FBDRV_PROVIDE:
        ServeProvide(granted);
        granted = -1;
        break;
    case FBDRV_SETBUF:
    case FBDRV_NOTIFY:
    case FBDRV_WATCH: {
        FbDrvRequest dreq = {0};
        memcpy(&dreq, GetMessageBox(), r->xlen < sizeof(dreq) ? r->xlen : sizeof(dreq));
        ServeDriverGrant(dreq.cmd, dreq.arg, granted);
        granted = -1;
        break;
    }
    case FB_CMD_GET_INFO:
        ServeGetInfo();
        break;
    case FB_CMD_GET_BUF:
        ServeGetBuf(granted);
        granted = -1;
        break;
    case FB_CMD_SWITCH:
        ReplyStatus(StartSwitch(req.arg));
        break;
    case INPUT_CMD_PUSH:
        ServeInputPush(r->xlen);
        break;
    case FB_CMD_RELEASE:
    case FB_CMD_SET_EVENT:
    case FB_CMD_FLIP:
    case FB_CMD_SET_VSYNC:
    case FB_CMD_DEACTIVATED:
    case FB_CMD_INPUT_OPEN:
        ServeClientCmd(&req, &granted);
        break;
    default:
        ReplyStatus(ERR_NOSYS);
        break;
    }
out:
    if (granted >= 0)
        HandleClose(granted);
}

static void ServiceDisplay(void)
{
    if (!g_disp.ctl)
        return;
    if (g_disp.live >= 0 && !HandleIsAlive(g_disp.live)) {
        LOG_INFO(LOG_TAG, "display driver died");
        DropDisplay();
        return;
    }
    if (!g_disp.ready)
        return;

    ReapClients();
    if (g_switch_to != NO_VT && UptimeMs() >= g_switch_deadline) {
        LOG_WARN(LOG_TAG, "vt%d did not release the display in time", g_active + 1);
        FinishSwitch();
    }

    FBDRV_FENCE();
    uint32_t bits = 0;
    if (g_disp.flip_pending && g_disp.ctl->flip_ack == g_disp.flip_sent) {
        g_disp.flip_pending = false;
        g_disp.shown = g_disp.flip_target;
        if (!g_disp.flip_quiet)
            bits |= FB_EVT_FLIPPED;
        if (g_disp.want != NO_IMAGE)
            ShowImage(g_disp.want);
    }
    uint32_t vsync = g_disp.ctl->vsync_seq;
    if (vsync != g_disp.vsync_seen) {
        g_disp.vsync_seen = vsync;
        bits |= FB_EVT_VSYNC;
    }
    if (bits && g_switch_to == NO_VT)
        ClientSignal(g_active, bits);
}

int main(void)
{
    for (uint32_t vt = 0; vt < FB_MAX_VTS; vt++)
        g_clients[vt] = (Client){.live = -1, .event = -1, .ring_mem = -1};

    g_port = CreatePort();
    g_event = CreateEvent();
    if (g_port < 0 || g_event < 0)
        return ERR_NOMEM;

    Err rc = Bind(EVENT_PORT, g_event, g_port, BIT_PORT);
    if (rc != ZUZU_OK)
        return rc;

    rc = RegisterService("/svc/gfx", g_port);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "register failed: %d", rc);
        return rc;
    }
    LOG_INFO(LOG_TAG, "serving /svc/gfx");

    for (;;) {
        WaitOn(g_event, POLL_MS);
        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }
        ServiceDisplay();
    }
}
