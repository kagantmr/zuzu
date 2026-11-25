#ifndef WM_INTERNAL_H
#define WM_INTERNAL_H

#include <dev/protocols/input.h>
#include <dev/protocols/wm.h>
#include <stdbool.h>
#include <stdint.h>
#include <types.h>

#define WM_MAX_CLIENTS 16

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} Rect;

typedef struct {
    Handle live;
    uint32_t token;
} Client;

typedef struct {
    bool used;
    int client;
    uint32_t id;
    uint32_t layer;
    Rect surf;
    char title[WM_TITLE_MAX];
    Handle mem;
    const uint16_t *px;
    Handle event;
    uint32_t shift;
    Handle ring_mem;
    InputRing *ring;
    bool minimized;
    bool frame_due;
} Window;

extern Window g_win[WM_MAX_WINDOWS];
extern int g_stack[WM_MAX_WINDOWS];
extern int g_nstack;
extern int g_focus;
extern int32_t g_screen_w;
extern int32_t g_screen_h;

bool RectEmpty(Rect r);
Rect RectIntersect(Rect a, Rect b);
Rect RectUnion(Rect a, Rect b);
bool RectContains(Rect r, int32_t x, int32_t y);

Rect WinOuter(const Window *w);
Rect WinTitleBar(const Window *w);
Rect WinCloseButton(const Window *w);
Rect WinMinButton(const Window *w);
bool WinVisible(const Window *w);

void ComposeInit(uint16_t *fb, uint32_t stride_px);
void Damage(Rect r);
void DamageAll(void);
bool ComposeDirty(void);
void ComposeFlush(bool to_screen);
void CursorMove(int32_t x, int32_t y, bool to_screen);

void WinSignal(int i, uint32_t bits);
void WinNotifyAll(uint32_t bits);
void WinRaise(int i);
void WinFocus(int i);
void WinFocusTopmost(void);
void WinMinimize(int i);
void WinDestroy(int i);
int WinAt(int32_t x, int32_t y);

Err ServerInit(Handle event, uint32_t bit);
void ServerDrain(void);
void ServerReap(void);

void InputPointer(int32_t x, int32_t y);
void InputButton(uint16_t code, bool down);
void InputKey(uint16_t usage, bool down);
void InputSync(void);
void InputWindowGone(int i);

#endif /* WM_INTERNAL_H */
