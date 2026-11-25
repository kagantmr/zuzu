/**
 * @file
 * @brief LVGL display and input glue for a wm window.
 */

#define LV_CONF_INCLUDE_SIMPLE
#include "lvgl.h"

#include <dev/hid.h>
#include <dev/keymap.h>
#include <string.h>
#include <zgfx/lv_win.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define CONNECT_WAIT_MS 5000
#define FRAME_TIMEOUT_MS 100
#define IDLE_MAX_MS 50
#define INPUT_BATCH 16
#define KEY_QUEUE 32

static WinConn g_conn;
static Win g_win;
static Handle g_event = -1;
static LvWinWake g_wake[32];
static LvWinWake g_on_windows;
static lv_area_t g_damage;
static bool g_has_damage;

static lv_point_t g_ptr;
static bool g_ptr_down;
static KeymapState g_keymap;
static uint32_t g_key_of_usage[256];
static struct {
    uint32_t key;
    bool pressed;
} g_keys[KEY_QUEUE];
static uint32_t g_key_head;
static uint32_t g_key_tail;
static uint32_t g_last_key;

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

static void OnKey(uint8_t usage, bool pressed)
{
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
    while ((n = WinReadInput(&g_win, ev, INPUT_BATCH)) > 0) {
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
    (void)px_map;
    if (!g_has_damage) {
        g_damage = *area;
        g_has_damage = true;
    } else {
        g_damage.x1 = LV_MIN(g_damage.x1, area->x1);
        g_damage.y1 = LV_MIN(g_damage.y1, area->y1);
        g_damage.x2 = LV_MAX(g_damage.x2, area->x2);
        g_damage.y2 = LV_MAX(g_damage.y2, area->y2);
    }
    if (lv_display_flush_is_last(disp)) {
        g_has_damage = false;
        WaitOnEvent(g_event, TIMEOUT_POLL, WM_EVT_FRAME, false);
        if (WinDamage(&g_win, g_damage.x1, g_damage.y1, (uint32_t)lv_area_get_width(&g_damage),
                      (uint32_t)lv_area_get_height(&g_damage)) == ZUZU_OK)
            WaitOnEvent(g_event, FRAME_TIMEOUT_MS, WM_EVT_FRAME, false);
    }
    lv_display_flush_ready(disp);
}

static uint32_t TickCb(void) { return (uint32_t)UptimeMs(); }

Err LvWinOpen(const WinSpec *spec)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return (Err)g_event;
    Err rc = WinConnect(&g_conn, CONNECT_WAIT_MS);
    if (rc != ZUZU_OK)
        return rc;
    WinSpec s = *spec;
    if (s.width == 0)
        s.width = g_conn.screen_w;
    if (s.y < 0)
        s.y += (int32_t)g_conn.screen_h;
    rc = WinCreate(&g_conn, &s, g_event, 0, &g_win);
    if (rc != ZUZU_OK)
        return rc;

    lv_init();
    lv_tick_set_cb(TickCb);
    lv_display_t *disp = lv_display_create((int32_t)s.width, (int32_t)s.height);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers_with_stride(disp, g_win.px, NULL,
                                       s.width * s.height * sizeof(uint16_t),
                                       s.width * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, FlushCb);

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, PointerRead);

    KeymapInit(&g_keymap);
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_t *keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad, KeypadRead);
    lv_indev_set_group(keypad, group);

    lv_theme_t *theme = lv_theme_default_init(disp, lv_color_hex(0x2196f3), lv_color_hex(0xf85149),
                                              true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
    return ZUZU_OK;
}

void LvWinOnWake(uint32_t bit, LvWinWake fn)
{
    if (bit >= LVWIN_FIRST_USER_BIT && bit < 32U)
        g_wake[bit] = fn;
}

void LvWinOnWindows(LvWinWake fn) { g_on_windows = fn; }

void LvWinRun(void)
{
    for (;;) {
        uint32_t next = lv_timer_handler();
        Duration wait = (next == LV_NO_TIMER_READY || next > IDLE_MAX_MS) ? IDLE_MAX_MS : next;
        EventWaitResult w = WaitOnEvent(g_event, wait, ~(EventWord)WM_EVT_FRAME, false);
        if (w.status != ZUZU_OK)
            continue;
        if (w.bits & WM_EVT_CLOSE)
            break;
        if ((w.bits & WM_EVT_WINDOWS) && g_on_windows)
            g_on_windows();
        for (uint32_t bit = LVWIN_FIRST_USER_BIT; bit < 32U; bit++) {
            if ((w.bits & (1U << bit)) && g_wake[bit])
                g_wake[bit]();
        }
    }
    WinClose(&g_win);
}

Handle LvWinEvent(void) { return g_event; }

WinConn *LvWinConn(void) { return &g_conn; }

Win *LvWinWindow(void) { return &g_win; }
