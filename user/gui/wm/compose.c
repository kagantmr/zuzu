#include "wm.h"

#include <stdlib.h>
#include <string.h>
#include <util/font12x16.h>

#include "logo.h"

#define MAX_DIRTY 32
#define BUTTON_W 26
#define BUTTON_H 20
#define CURSOR_W 12
#define CURSOR_H 19

#define COL_BORDER RGB565(0x30, 0x36, 0x3d)
#define COL_BORDER_FOCUS RGB565(0x21, 0x96, 0xf3)
#define COL_TITLE RGB565(0x1c, 0x26, 0x33)
#define COL_TITLE_FOCUS RGB565(0x1f, 0x3b, 0x5c)
#define COL_TITLE_TEXT RGB565(0xe6, 0xed, 0xf3)
#define COL_TITLE_DIM RGB565(0x8b, 0x98, 0xa5)
#define COL_CLOSE RGB565(0xf8, 0x51, 0x49)
#define COL_WALL_TOP 0x1d3557U
#define COL_WALL_BOTTOM 0x0b1320U
#define WALL_LOGO_OPACITY 18U

static const char *const k_cursor[CURSOR_H] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
    "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
    "X.........X ", "X......XXXXX", "X...X..X    ", "X..XX..X    ", "X.X  X..X   ",
    "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   ",
};

static uint16_t *g_shadow;
static uint16_t *g_fb;
static uint32_t g_stride;
static uint16_t *g_wall;
static Rect g_dirty[MAX_DIRTY];
static int g_ndirty;
static int32_t g_cx;
static int32_t g_cy;

bool RectEmpty(Rect r) { return r.w <= 0 || r.h <= 0; }

Rect RectIntersect(Rect a, Rect b)
{
    int32_t x1 = a.x > b.x ? a.x : b.x;
    int32_t y1 = a.y > b.y ? a.y : b.y;
    int32_t x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return (Rect){x1, y1, x2 - x1, y2 - y1};
}

Rect RectUnion(Rect a, Rect b)
{
    if (RectEmpty(a))
        return b;
    if (RectEmpty(b))
        return a;
    int32_t x1 = a.x < b.x ? a.x : b.x;
    int32_t y1 = a.y < b.y ? a.y : b.y;
    int32_t x2 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return (Rect){x1, y1, x2 - x1, y2 - y1};
}

bool RectContains(Rect r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

Rect WinOuter(const Window *w)
{
    if (w->layer != WM_LAYER_NORMAL)
        return w->surf;
    return (Rect){w->surf.x - WM_BORDER, w->surf.y - WM_TITLE_H - WM_BORDER,
                  w->surf.w + 2 * WM_BORDER, w->surf.h + WM_TITLE_H + 2 * WM_BORDER};
}

Rect WinTitleBar(const Window *w)
{
    return (Rect){w->surf.x, w->surf.y - WM_TITLE_H, w->surf.w, WM_TITLE_H};
}

Rect WinCloseButton(const Window *w)
{
    Rect t = WinTitleBar(w);
    return (Rect){t.x + t.w - BUTTON_W - 4, t.y + (WM_TITLE_H - BUTTON_H) / 2, BUTTON_W, BUTTON_H};
}

Rect WinMinButton(const Window *w)
{
    Rect c = WinCloseButton(w);
    c.x -= BUTTON_W + 4;
    return c;
}

bool WinVisible(const Window *w) { return w->used && !w->minimized && w->px; }

uint16_t Lerp565(uint32_t a, uint32_t b, int32_t num, int32_t den)
{
    uint32_t r = ((a >> 16) & 0xFFU) +
                 (uint32_t)((int32_t)(((b >> 16) & 0xFFU) - ((a >> 16) & 0xFFU)) * num / den);
    uint32_t g = ((a >> 8) & 0xFFU) +
                 (uint32_t)((int32_t)(((b >> 8) & 0xFFU) - ((a >> 8) & 0xFFU)) * num / den);
    uint32_t bl = (a & 0xFFU) + (uint32_t)((int32_t)((b & 0xFFU) - (a & 0xFFU)) * num / den);
    return RGB565(r, g, bl);
}

static uint16_t Mix565(uint16_t dst, uint32_t rgb, uint32_t a)
{
    uint32_t dr = (dst >> 11) & 0x1FU;
    uint32_t dg = (dst >> 5) & 0x3FU;
    uint32_t db = dst & 0x1FU;
    uint32_t sr = (rgb >> 19) & 0x1FU;
    uint32_t sg = (rgb >> 10) & 0x3FU;
    uint32_t sb = (rgb >> 3) & 0x1FU;
    dr = (dr * (255U - a) + sr * a) / 255U;
    dg = (dg * (255U - a) + sg * a) / 255U;
    db = (db * (255U - a) + sb * a) / 255U;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static void BlendInto(uint16_t *buf, Rect clip, int32_t x, int32_t y, const uint8_t *mask,
                      int32_t w, int32_t h, uint32_t rgb, uint32_t opacity)
{
    Rect r = RectIntersect(clip, (Rect){x, y, w, h});
    for (int32_t py = r.y; py < r.y + r.h; py++) {
        const uint8_t *m = mask + ((py - y) * w) + (r.x - x);
        uint16_t *row = buf + (py * g_screen_w) + r.x;
        for (int32_t px = 0; px < r.w; px++) {
            uint32_t a = (uint32_t)m[px] * opacity / 255U;
            if (a)
                row[px] = Mix565(row[px], rgb, a);
        }
    }
}

void Blend(Rect clip, int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h, uint32_t rgb,
           uint32_t opacity)
{
    BlendInto(g_shadow, clip, x, y, mask, w, h, rgb, opacity);
}

void ComposeInit(uint16_t *fb, uint32_t stride_px)
{
    g_fb = fb;
    g_stride = stride_px;
    size_t pixels = (size_t)g_screen_w * (size_t)g_screen_h;
    g_shadow = malloc(pixels * sizeof(uint16_t));
    g_wall = malloc(pixels * sizeof(uint16_t));
    for (int32_t y = 0; y < g_screen_h; y++) {
        uint16_t c = Lerp565(COL_WALL_TOP, COL_WALL_BOTTOM, y, g_screen_h);
        for (int32_t x = 0; x < g_screen_w; x++)
            g_wall[(y * g_screen_w) + x] = c;
    }
    Rect screen = {0, 0, g_screen_w, g_screen_h};
    BlendInto(g_wall, screen, (g_screen_w - LOGO_W) / 2, (g_screen_h - LOGO_H) / 2 - 22, k_logo,
              LOGO_W, LOGO_H, 0xFFFFFFU, WALL_LOGO_OPACITY);
    g_cx = g_screen_w / 2;
    g_cy = g_screen_h / 2;
    DamageAll();
}

void Damage(Rect r)
{
    r = RectIntersect(r, (Rect){0, 0, g_screen_w, g_screen_h});
    if (RectEmpty(r))
        return;
    for (int i = 0; i < g_ndirty; i++) {
        Rect u = RectUnion(g_dirty[i], r);
        if ((int64_t)u.w * u.h <= (int64_t)g_dirty[i].w * g_dirty[i].h + (int64_t)r.w * r.h) {
            g_dirty[i] = u;
            return;
        }
    }
    if (g_ndirty == MAX_DIRTY) {
        for (int i = 1; i < g_ndirty; i++)
            g_dirty[0] = RectUnion(g_dirty[0], g_dirty[i]);
        g_ndirty = 1;
        g_dirty[0] = RectUnion(g_dirty[0], r);
        return;
    }
    g_dirty[g_ndirty++] = r;
}

void DamageAll(void)
{
    g_ndirty = 1;
    g_dirty[0] = (Rect){0, 0, g_screen_w, g_screen_h};
}

bool ComposeDirty(void) { return g_ndirty > 0; }

void Fill(Rect clip, Rect r, uint16_t color)
{
    r = RectIntersect(r, clip);
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        uint16_t *row = g_shadow + (y * g_screen_w) + r.x;
        for (int32_t x = 0; x < r.w; x++)
            row[x] = color;
    }
}

void Text(Rect clip, int32_t x, int32_t y, const char *s, int32_t max_w, uint16_t color)
{
    for (; *s && max_w >= FONT_GLYPH_W; s++, x += FONT_GLYPH_W, max_w -= FONT_GLYPH_W) {
        uint32_t c = (uint8_t)*s;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR)
            c = '?';
        const uint16_t *glyph = FONT12X16[c - FONT_FIRST_CHAR];
        Rect g = RectIntersect(clip, (Rect){x, y, FONT_GLYPH_W, FONT_GLYPH_H});
        for (int32_t py = g.y; py < g.y + g.h; py++) {
            uint16_t bits = glyph[py - y];
            uint16_t *row = g_shadow + (py * g_screen_w);
            for (int32_t px = g.x; px < g.x + g.w; px++) {
                if ((bits >> (FONT_GLYPH_W - 1 - (px - x))) & 1U)
                    row[px] = color;
            }
        }
    }
}

static void DrawFrame(Rect clip, const Window *w, bool focused)
{
    Rect outer = WinOuter(w);
    uint16_t border = focused ? COL_BORDER_FOCUS : COL_BORDER;
    Fill(clip, (Rect){outer.x, outer.y, outer.w, WM_BORDER}, border);
    Fill(clip, (Rect){outer.x, outer.y + outer.h - WM_BORDER, outer.w, WM_BORDER}, border);
    Fill(clip, (Rect){outer.x, outer.y, WM_BORDER, outer.h}, border);
    Fill(clip, (Rect){outer.x + outer.w - WM_BORDER, outer.y, WM_BORDER, outer.h}, border);

    Rect bar = WinTitleBar(w);
    if (RectEmpty(RectIntersect(clip, bar)))
        return;
    Fill(clip, bar, focused ? COL_TITLE_FOCUS : COL_TITLE);
    Rect close = WinCloseButton(w);
    Rect min = WinMinButton(w);
    int32_t ty = bar.y + (WM_TITLE_H - FONT_GLYPH_H) / 2;
    Text(clip, bar.x + 10, ty, w->title, min.x - bar.x - 16,
         focused ? COL_TITLE_TEXT : COL_TITLE_DIM);
    Fill(clip, (Rect){close.x, close.y, close.w, close.h}, focused ? COL_CLOSE : COL_TITLE);
    Text(clip, close.x + (BUTTON_W - FONT_GLYPH_W) / 2, close.y + 2, "x", FONT_GLYPH_W,
         COL_TITLE_TEXT);
    Fill(clip, (Rect){min.x + 7, min.y + min.h - 6, BUTTON_W - 14, 2}, COL_TITLE_TEXT);
}

static void DrawSurface(Rect clip, const Window *w)
{
    Rect r = RectIntersect(clip, w->surf);
    if (RectEmpty(r))
        return;
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        const uint16_t *src = w->px + ((y - w->surf.y) * w->surf.w) + (r.x - w->surf.x);
        memcpy(g_shadow + (y * g_screen_w) + r.x, src, (size_t)r.w * sizeof(uint16_t));
    }
}

static bool Covered(Rect r, int from)
{
    for (int s = from; s < g_nstack; s++) {
        const Window *w = &g_win[g_stack[s]];
        if (!WinVisible(w))
            continue;
        Rect o = w->surf;
        if (r.x >= o.x && r.y >= o.y && r.x + r.w <= o.x + o.w && r.y + r.h <= o.y + o.h)
            return true;
    }
    return false;
}

static void Paint(Rect clip)
{
    if (g_booting) {
        SplashPaint(clip);
        return;
    }
    int first = 0;
    for (int s = g_nstack - 1; s >= 0; s--) {
        if (Covered(clip, s)) {
            first = s;
            break;
        }
    }
    if (first == 0 && !Covered(clip, 0)) {
        for (int32_t y = clip.y; y < clip.y + clip.h; y++)
            memcpy(g_shadow + (y * g_screen_w) + clip.x, g_wall + (y * g_screen_w) + clip.x,
                   (size_t)clip.w * sizeof(uint16_t));
    }
    for (int s = first; s < g_nstack; s++) {
        const Window *w = &g_win[g_stack[s]];
        if (!WinVisible(w) || RectEmpty(RectIntersect(clip, WinOuter(w))))
            continue;
        if (w->layer == WM_LAYER_NORMAL)
            DrawFrame(clip, w, g_stack[s] == g_focus);
        DrawSurface(clip, w);
    }
}

static void Present(Rect r)
{
    for (int32_t y = r.y; y < r.y + r.h; y++)
        memcpy(g_fb + (y * (int32_t)g_stride) + r.x, g_shadow + (y * g_screen_w) + r.x,
               (size_t)r.w * sizeof(uint16_t));
}

static Rect CursorRect(int32_t x, int32_t y) { return (Rect){x, y, CURSOR_W, CURSOR_H}; }

static void DrawCursor(void)
{
    Rect r = RectIntersect(CursorRect(g_cx, g_cy), (Rect){0, 0, g_screen_w, g_screen_h});
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        const char *line = k_cursor[y - g_cy];
        uint16_t *row = g_fb + (y * (int32_t)g_stride);
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            char c = line[x - g_cx];
            if (c == 'X')
                row[x] = 0x0000;
            else if (c == '.')
                row[x] = 0xFFFF;
        }
    }
}

void ComposeFlush(bool to_screen)
{
    int n = g_ndirty;
    g_ndirty = 0;
    for (int i = 0; i < n; i++)
        Paint(g_dirty[i]);
    if (!to_screen)
        return;
    bool cursor = false;
    for (int i = 0; i < n; i++) {
        Present(g_dirty[i]);
        if (!RectEmpty(RectIntersect(g_dirty[i], CursorRect(g_cx, g_cy))))
            cursor = true;
    }
    if (cursor && !g_booting)
        DrawCursor();
}

void CursorMove(int32_t x, int32_t y, bool to_screen)
{
    if (x == g_cx && y == g_cy)
        return;
    Rect old = RectIntersect(CursorRect(g_cx, g_cy), (Rect){0, 0, g_screen_w, g_screen_h});
    g_cx = x;
    g_cy = y;
    if (!to_screen || g_booting)
        return;
    if (!RectEmpty(old))
        Present(old);
    DrawCursor();
}
