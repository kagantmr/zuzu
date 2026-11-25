#include "gui.h"

#include <dev/hid.h>
#include <dev/keymap.h>
#include <dev/protocols/fb.h>
#include <dev/protocols/input.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "gui"
#include <util/log.h>

#define FLIP_TIMEOUT_MS 100
#define IDLE_MAX_MS 50
#define DISPLAY_WAIT_TRIES 500
#define DISPLAY_WAIT_MS 10
#define VT_EVENTS (FB_EVT_ACTIVATE | FB_EVT_DEACTIVATE)
#define HOOK_BITS ((1U << GUI_BIT_TTY) | (1U << GUI_BIT_CHILD))
#define WAKE_EVENTS (VT_EVENTS | FB_EVT_INPUT | HOOK_BITS)
#define INPUT_BATCH 16
#define KEY_QUEUE 32

static Handle g_gfx = -1;
static Handle g_event = -1;
static uint32_t g_token;
static FbInfo g_info;
static uint8_t *g_fb;
static bool g_active;
static InputRing *g_ring;
static lv_point_t g_ptr;
static bool g_ptr_down;
static GuiKeyHook g_key_hook;
static GuiWakeHook g_wake_hooks[32];
static KeymapState g_keymap;
static uint32_t g_key_of_usage[256];
static struct {
    uint32_t key;
    bool pressed;
} g_keys[KEY_QUEUE];
static uint32_t g_key_head;
static uint32_t g_key_tail;
static uint32_t g_last_key;

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

static uint32_t SpecialKey(uint8_t usage, bool shift)
{
    switch (usage) {
    case HID_KEY_ENTER:
    case HID_KEY_KP_ENTER:
        return LV_KEY_ENTER;
    case HID_KEY_ESCAPE:
        return LV_KEY_ESC;
    case HID_KEY_BACKSPACE:
        return LV_KEY_BACKSPACE;
    case HID_KEY_DELETE:
        return LV_KEY_DEL;
    case HID_KEY_TAB:
        return shift ? LV_KEY_PREV : LV_KEY_NEXT;
    case HID_KEY_UP:
        return LV_KEY_UP;
    case HID_KEY_DOWN:
        return LV_KEY_DOWN;
    case HID_KEY_LEFT:
        return LV_KEY_LEFT;
    case HID_KEY_RIGHT:
        return LV_KEY_RIGHT;
    case HID_KEY_HOME:
        return LV_KEY_HOME;
    case HID_KEY_END:
        return LV_KEY_END;
    default:
        return 0;
    }
}

static void QueueKey(uint32_t key, bool pressed)
{
    if (g_key_head - g_key_tail == KEY_QUEUE)
        return;
    g_keys[g_key_head % KEY_QUEUE].key = key;
    g_keys[g_key_head % KEY_QUEUE].pressed = pressed;
    g_key_head++;
}

Handle GuiEvent(void) { return g_event; }

void GuiSetKeyHook(GuiKeyHook hook) { g_key_hook = hook; }

void GuiSetWakeHook(uint32_t bit, GuiWakeHook hook)
{
    if (bit < 32U)
        g_wake_hooks[bit] = hook;
}

static void OnKey(uint8_t usage, bool pressed)
{
    if (g_key_hook) {
        g_key_hook(usage, pressed);
        return;
    }
    bool shift = (g_keymap.mods & (KEYMOD_LSHIFT | KEYMOD_RSHIFT)) != 0;
    uint8_t seq[KEYMAP_MAX_SEQ];
    size_t n = KeymapFeed(&g_keymap, usage, pressed, seq, sizeof(seq));

    if (!pressed) {
        if (g_key_of_usage[usage]) {
            QueueKey(g_key_of_usage[usage], false);
            g_key_of_usage[usage] = 0;
        }
        return;
    }
    uint32_t key = SpecialKey(usage, shift);
    if (!key && n == 1 && seq[0] >= 0x20 && seq[0] < 0x7F)
        key = seq[0];
    if (!key)
        return;
    g_key_of_usage[usage] = key;
    QueueKey(key, true);
}

static void DrainInput(void)
{
    InputEvent ev[INPUT_BATCH];
    uint32_t n;
    while ((n = InputRingRead(g_ring, ev, INPUT_BATCH)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            if (ev[i].type == INPUT_EV_ABS && ev[i].code == INPUT_ABS_X)
                g_ptr.x = ev[i].value;
            else if (ev[i].type == INPUT_EV_ABS && ev[i].code == INPUT_ABS_Y)
                g_ptr.y = ev[i].value;
            else if (ev[i].type == INPUT_EV_BTN && ev[i].code == INPUT_BTN_LEFT)
                g_ptr_down = ev[i].value != 0;
            else if (ev[i].type == INPUT_EV_KEY)
                OnKey((uint8_t)ev[i].code, ev[i].value != 0);
        }
    }
}

static void PointerRead(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    DrainInput();
    data->point = g_ptr;
    data->state = g_ptr_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void KeypadRead(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    DrainInput();
    if (g_key_tail == g_key_head) {
        data->key = g_last_key;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    uint32_t i = g_key_tail++ % KEY_QUEUE;
    g_last_key = g_keys[i].key;
    data->key = g_keys[i].key;
    data->state = g_keys[i].pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = g_key_tail != g_key_head;
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
        KeymapInit(&g_keymap);
        memset(g_key_of_usage, 0, sizeof(g_key_of_usage));
        g_ptr_down = false;
        FbCall(FB_CMD_DEACTIVATED, 0, -1, NULL, 0, NULL);
    }
    if (bits & FB_EVT_ACTIVATE) {
        g_active = true;
        lv_obj_invalidate(lv_screen_active());
    }
    for (uint32_t bit = 0; bit < 32U; bit++) {
        if ((bits & (1U << bit)) && g_wake_hooks[bit])
            g_wake_hooks[bit]();
    }
}

static uint32_t TickCb(void) { return (uint32_t)UptimeMs(); }

static void CreatePointer(void)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, PointerRead);

    lv_obj_t *cursor = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(cursor);
    lv_obj_set_size(cursor, 10, 10);
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cursor, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(cursor, 1, 0);
    lv_obj_set_style_border_color(cursor, lv_color_hex(0x000000), 0);
    lv_indev_set_cursor(indev, cursor);
}

static void CreateKeypad(void)
{
    KeymapInit(&g_keymap);
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, KeypadRead);
    lv_indev_set_group(indev, group);
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

    CreateKeypad();
    CreatePointer();
    ShellInit();

    for (;;) {
        Duration wait = TIMEOUT_INFINITE;
        if (g_active) {
            uint32_t next = lv_timer_handler();
            wait = (next == LV_NO_TIMER_READY || next > IDLE_MAX_MS) ? IDLE_MAX_MS : next;
        }
        EventWaitResult w = WaitOnEvent(g_event, wait, WAKE_EVENTS, false);
        if (w.status == ZUZU_OK)
            HandleVtEvents(w.bits);
    }
}
