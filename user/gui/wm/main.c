#include "wm.h"

#include <dev/protocols/fb.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "wm"
#include <util/log.h>

#define BIT_PORT 8
#define REAP_MS 100
#define SPLASH_FRAME_MS 33
#define DISPLAY_WAIT_TRIES 500
#define DISPLAY_WAIT_MS 10
#define INPUT_BATCH 16
#define WAKE_EVENTS (FB_EVT_ACTIVATE | FB_EVT_DEACTIVATE | FB_EVT_INPUT | (1U << BIT_PORT))

int32_t g_screen_w;
int32_t g_screen_h;

static Handle g_gfx = -1;
static Handle g_event = -1;
static uint32_t g_token;
static FbInfo g_info;
static uint8_t *g_fb;
static bool g_active;
static bool g_need_flip;
static InputRing *g_ring;

static Err FbCall(uint32_t cmd, uint32_t arg, Handle grant, void *reply, uint32_t reply_len,
                  Handle *reply_grant)
{
    FbRequest req = {.cmd = cmd, .token = g_token, .arg = arg};
    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(g_gfx, sizeof(req), grant, &got, reply_grant);
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

static Err FbOpen(void)
{
    g_gfx = LookupService("/svc/gfx");
    if (g_gfx < 0)
        return ERR_NOENT;

    Err rc = ERR_NOENT;
    for (int i = 0; i < DISPLAY_WAIT_TRIES; i++) {
        rc = FbCall(FB_CMD_GET_INFO, 0, -1, &g_info, sizeof(g_info), NULL);
        if (rc != ERR_NOENT)
            break;
        Sleep(DISPLAY_WAIT_MS);
    }
    if (rc != ZUZU_OK)
        return rc;
    if (g_info.format != FB_FMT_RGB565)
        return ERR_NOSYS;

    Handle live = CreatePort();
    if (live < 0)
        return (Err)live;
    Handle dup;
    rc = HandleDup(live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    Handle mem = -1;
    rc = FbCall(FB_CMD_GET_BUF, 0, dup, &g_info, sizeof(g_info), &mem);
    HandleClose(dup);
    if (rc != ZUZU_OK)
        return rc;
    if (mem < 0)
        return ERR_MALFORMED;
    g_token = g_info.token;
    g_active = g_info.active != 0;

    g_fb = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(g_fb))
        return (Err)(intptr_t)g_fb;

    rc = HandleDup(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    rc = FbCall(FB_CMD_SET_EVENT, 0, dup, NULL, 0, NULL);
    HandleClose(dup);
    if (rc != ZUZU_OK)
        return rc;

    Handle ring = -1;
    rc = FbCall(FB_CMD_INPUT_OPEN, 0, -1, NULL, 0, &ring);
    if (rc != ZUZU_OK || ring < 0)
        return rc != ZUZU_OK ? rc : ERR_MALFORMED;
    g_ring = MemMap(ring, 0, PROT_RW);
    if (PtrIsErr(g_ring))
        return (Err)(intptr_t)g_ring;
    return ZUZU_OK;
}

static void DrainInput(void)
{
    InputEvent ev[INPUT_BATCH];
    uint32_t n;
    int32_t x = -1;
    int32_t y = -1;
    while ((n = InputRingRead(g_ring, ev, INPUT_BATCH)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            if (g_booting)
                continue;
            switch (ev[i].type) {
            case INPUT_EV_ABS:
                if (ev[i].code == INPUT_ABS_X)
                    x = ev[i].value;
                else if (ev[i].code == INPUT_ABS_Y)
                    y = ev[i].value;
                break;
            case INPUT_EV_SYN:
                if (x >= 0 || y >= 0) {
                    InputPointer(x >= 0 ? x : 0, y >= 0 ? y : 0);
                    CursorMove(x >= 0 ? x : 0, y >= 0 ? y : 0, g_active);
                }
                break;
            case INPUT_EV_BTN:
                InputButton(ev[i].code, ev[i].value != 0);
                break;
            case INPUT_EV_KEY:
                InputKey(ev[i].code, ev[i].value != 0);
                break;
            default:
                break;
            }
        }
    }
    if (!g_booting)
        InputSync();
}

static void HandleVt(uint32_t bits)
{
    if (bits & FB_EVT_DEACTIVATE) {
        g_active = false;
        FbCall(FB_CMD_DEACTIVATED, 0, -1, NULL, 0, NULL);
    }
    if (bits & FB_EVT_ACTIVATE) {
        g_active = true;
        g_need_flip = true;
        DamageAll();
    }
}

static void SignalFrames(void)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].frame_due) {
            g_win[i].frame_due = false;
            WinSignal(i, WM_EVT_FRAME);
        }
    }
}

int main(void)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return ERR_NOMEM;
    Err rc = FbOpen();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "display unavailable: %s", StrToError(rc));
        return rc;
    }
    g_screen_w = (int32_t)g_info.width;
    g_screen_h = (int32_t)g_info.height;
    LOG_INFO(LOG_TAG, "%dx%d on vt%u", g_screen_w, g_screen_h, g_info.vt);

    ComposeInit((uint16_t *)g_fb, g_info.stride / sizeof(uint16_t));
    rc = ServerInit(g_event, BIT_PORT);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "cannot serve /svc/wm: %s", StrToError(rc));
        return rc;
    }
    SplashStart(UptimeMs());
    if (!g_active)
        FbCall(FB_CMD_SWITCH, g_info.vt, -1, NULL, 0, NULL);
    g_need_flip = g_active;

    for (;;) {
        Duration wait = g_booting ? SPLASH_FRAME_MS : REAP_MS;
        EventWaitResult w = WaitOnEvent(g_event, wait, WAKE_EVENTS, false);
        SplashTick(UptimeMs());
        uint32_t bits = w.status == ZUZU_OK ? w.bits : 0;
        if (bits & (FB_EVT_ACTIVATE | FB_EVT_DEACTIVATE))
            HandleVt(bits);
        ServerDrain();
        if (g_ring)
            DrainInput();
        ServerReap();
        if (ComposeDirty()) {
            ComposeFlush(g_active);
            SignalFrames();
        }
        if (g_active && g_need_flip && FbCall(FB_CMD_FLIP, 0, -1, NULL, 0, NULL) == ZUZU_OK)
            g_need_flip = false;
    }
}
