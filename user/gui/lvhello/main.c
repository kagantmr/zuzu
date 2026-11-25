#define LV_CONF_INCLUDE_SIMPLE
#include "vendor/lvgl/lvgl.h"

#include <dev/protocols/fb.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "lvhello"
#include <util/log.h>

#define FLIP_TIMEOUT_MS 100
#define IDLE_MAX_MS 50
#define DISPLAY_WAIT_TRIES 500
#define DISPLAY_WAIT_MS 10
#define VT_EVENTS (FB_EVT_ACTIVATE | FB_EVT_DEACTIVATE)

static Handle g_gfx = -1;
static Handle g_event = -1;
static uint32_t g_token;
static FbInfo g_info;
static uint8_t *g_fb;
static bool g_active;

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

    g_event = CreateEvent();
    if (g_event < 0)
        return (Err)g_event;
    rc = HandleDup(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    rc = FbCall(FB_CMD_SET_EVENT, 0, dup, NULL, 0, NULL);
    HandleClose(dup);
    return rc;
}

static void FlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    if (g_info.buffers < 2 || !lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    uint32_t index = (uint32_t)(px_map - g_fb) / g_info.buf_size;
    WaitOnEvent(g_event, TIMEOUT_POLL, FB_EVT_FLIPPED, false);
    if (FbCall(FB_CMD_FLIP, index, -1, NULL, 0, NULL) == ZUZU_OK)
        WaitOnEvent(g_event, FLIP_TIMEOUT_MS, FB_EVT_FLIPPED, false);
    lv_display_flush_ready(disp);
}

static void HandleVtEvents(uint32_t bits)
{
    if (bits & FB_EVT_DEACTIVATE) {
        g_active = false;
        FbCall(FB_CMD_DEACTIVATED, 0, -1, NULL, 0, NULL);
    }
    if (bits & FB_EVT_ACTIVATE) {
        g_active = true;
        lv_obj_invalidate(lv_screen_active());
    }
}

static uint32_t TickCb(void) { return (uint32_t)UptimeMs(); }

static void BuildUi(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e2a38), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello from zuzuOS + LVGL");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 40);
}

int main(void)
{
    Err rc = FbOpen();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "display unavailable: %s", StrToError(rc));
        return rc;
    }
    LOG_INFO(LOG_TAG, "%ux%u, %u image(s)", g_info.width, g_info.height, g_info.buffers);

    lv_init();
    lv_tick_set_cb(TickCb);

    lv_display_t *disp = lv_display_create((int32_t)g_info.width, (int32_t)g_info.height);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    void *buf2 = g_info.buffers >= 2 ? g_fb + g_info.buf_size : NULL;
    lv_display_set_buffers_with_stride(disp, g_fb, buf2, g_info.buf_size, g_info.stride,
                                       LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, FlushCb);

    BuildUi();

    for (;;) {
        Duration wait = TIMEOUT_INFINITE;
        if (g_active) {
            uint32_t next = lv_timer_handler();
            wait = (next == LV_NO_TIMER_READY || next > IDLE_MAX_MS) ? IDLE_MAX_MS : next;
        }
        EventWaitResult w = WaitOnEvent(g_event, wait, VT_EVENTS, false);
        if (w.status == ZUZU_OK)
            HandleVtEvents(w.bits);
    }
}
