#include "wm.h"

#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

static int32_t g_px;
static int32_t g_py;
static bool g_moved;
static int g_grab = -1;
static int g_drag = -1;
static int32_t g_drag_dx;
static int32_t g_drag_dy;
static uint32_t g_pending;

static void Send(int i, uint16_t type, uint16_t code, int32_t value)
{
    Window *w = &g_win[i];
    if (!w->used || !w->ring)
        return;
    InputEvent ev = {.type = type, .code = code, .value = value, .time_ms = (uint32_t)UptimeMs()};
    if (InputRingWrite(w->ring, &ev))
        g_pending |= 1U << i;
}

static void SendPointer(int i)
{
    Send(i, INPUT_EV_ABS, INPUT_ABS_X, g_px - g_win[i].surf.x);
    Send(i, INPUT_EV_ABS, INPUT_ABS_Y, g_py - g_win[i].surf.y);
}

static int SurfaceAt(int32_t x, int32_t y)
{
    int i = WinAt(x, y);
    if (i >= 0 && RectContains(g_win[i].surf, x, y))
        return i;
    return -1;
}

static void MoveDragged(void)
{
    Window *w = &g_win[g_drag];
    Rect before = WinOuter(w);
    int32_t x = g_px - g_drag_dx;
    int32_t y = g_py - g_drag_dy;
    int32_t min_y = WM_TITLE_H + WM_BORDER;
    if (y < min_y)
        y = min_y;
    if (y > g_screen_h - 8)
        y = g_screen_h - 8;
    if (x + w->surf.w < 48)
        x = 48 - w->surf.w;
    if (x > g_screen_w - 48)
        x = g_screen_w - 48;
    if (x == w->surf.x && y == w->surf.y)
        return;
    w->surf.x = x;
    w->surf.y = y;
    Damage(RectUnion(before, WinOuter(w)));
}

void InputPointer(int32_t x, int32_t y)
{
    if (x == g_px && y == g_py)
        return;
    g_px = x;
    g_py = y;
    g_moved = true;
    if (g_drag >= 0)
        MoveDragged();
}

void InputButton(uint16_t code, bool down)
{
    if (code != INPUT_BTN_LEFT) {
        int i = g_grab >= 0 ? g_grab : SurfaceAt(g_px, g_py);
        if (i >= 0) {
            SendPointer(i);
            Send(i, INPUT_EV_BTN, code, down);
            Send(i, INPUT_EV_SYN, 0, 0);
        }
        return;
    }
    if (!down) {
        if (g_drag >= 0) {
            g_drag = -1;
            return;
        }
        if (g_grab >= 0) {
            SendPointer(g_grab);
            Send(g_grab, INPUT_EV_BTN, code, 0);
            Send(g_grab, INPUT_EV_SYN, 0, 0);
            g_grab = -1;
        }
        return;
    }

    int i = WinAt(g_px, g_py);
    if (i < 0)
        return;
    Window *w = &g_win[i];
    if (w->layer == WM_LAYER_NORMAL) {
        if (g_focus != i || g_stack[g_nstack - 1] != i)
            WinFocus(i);
        if (RectContains(WinTitleBar(w), g_px, g_py)) {
            if (RectContains(WinCloseButton(w), g_px, g_py)) {
                WinSignal(i, WM_EVT_CLOSE);
            } else if (RectContains(WinMinButton(w), g_px, g_py)) {
                WinMinimize(i);
            } else {
                g_drag = i;
                g_drag_dx = g_px - w->surf.x;
                g_drag_dy = g_py - w->surf.y;
            }
            return;
        }
    }
    if (!RectContains(w->surf, g_px, g_py))
        return;
    g_grab = i;
    SendPointer(i);
    Send(i, INPUT_EV_BTN, code, 1);
    Send(i, INPUT_EV_SYN, 0, 0);
}

void InputKey(uint16_t usage, bool down)
{
    if (g_focus < 0)
        return;
    Send(g_focus, INPUT_EV_KEY, usage, down);
    Send(g_focus, INPUT_EV_SYN, 0, 0);
}

void InputSync(void)
{
    if (g_moved && g_drag < 0) {
        int i = g_grab >= 0 ? g_grab : SurfaceAt(g_px, g_py);
        if (i >= 0) {
            SendPointer(i);
            Send(i, INPUT_EV_SYN, 0, 0);
        }
    }
    g_moved = false;
    for (int i = 0; g_pending && i < (int)WM_MAX_WINDOWS; i++) {
        if (g_pending & (1U << i))
            WinSignal(i, WM_EVT_INPUT);
    }
    g_pending = 0;
}

void InputWindowGone(int i)
{
    if (g_grab == i)
        g_grab = -1;
    if (g_drag == i)
        g_drag = -1;
    g_pending &= ~(1U << i);
}
