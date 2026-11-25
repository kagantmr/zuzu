#include <dev/protocols/fb.h>
#include <dev/protocols/fbdrv.h>
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
} Client;

static Handle g_port = -1;
static Handle g_event = -1;
static Display g_disp = {.ctl_mem = -1, .fb_mem = -1, .bell = -1, .live = -1, .want = NO_IMAGE};
static Client g_clients[FB_MAX_VTS];
static int g_active = NO_VT;
static int g_switch_to = NO_VT;
static uint64_t g_switch_deadline;
static uint32_t g_next_token = 1;

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

static void Activate(int vt)
{
    g_active = vt;
    ShowImage(g_clients[vt].image);
    ClientSignal(vt, FB_EVT_ACTIVATE);
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

static void DropClient(int vt)
{
    Client *c = &g_clients[vt];
    CloseIf(&c->live);
    CloseIf(&c->event);
    *c = (Client){.live = -1, .event = -1};

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
        if (ClientUsed(vt)) {
            CloseIf(&g_clients[vt].live);
            CloseIf(&g_clients[vt].event);
        }
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
    *c = (Client){.live = live, .event = -1, .token = g_next_token++, .image = 0};
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

static void ServeSwitch(uint32_t target)
{
    if (!g_disp.ready) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    ReapClients();
    int vt = (int)target - 1;
    if (target == 0 || target > FB_MAX_VTS || !ClientUsed(vt)) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    if (g_switch_to != NO_VT) {
        ReplyStatus(ERR_BUSY);
        return;
    }
    if (vt != g_active) {
        if (g_active == NO_VT) {
            Activate(vt);
        } else {
            g_switch_to = vt;
            g_switch_deadline = UptimeMs() + FB_SWITCH_TIMEOUT_MS;
            ClientSignal(g_active, FB_EVT_DEACTIVATE);
        }
    }
    ReplyStatus(ZUZU_OK);
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
        ServeSwitch(req.arg);
        break;
    case FB_CMD_RELEASE:
    case FB_CMD_SET_EVENT:
    case FB_CMD_FLIP:
    case FB_CMD_SET_VSYNC:
    case FB_CMD_DEACTIVATED:
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
        g_clients[vt] = (Client){.live = -1, .event = -1};

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
