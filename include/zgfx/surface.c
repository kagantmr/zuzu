#include "surface.h"

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

bool ZgfxRectIsEmpty(Rect r) { return (r.w <= 0 || r.h <= 0); }

Rect ZgfxRectIntersect(Rect a, Rect b)
{
    int left = MAX(a.x, b.x);
    int right = MIN(a.x + a.w, b.x + b.w);
    int top = MAX(a.y, b.y);
    int bottom = MIN(a.y + a.h, b.y + b.h);
    if (right <= left || bottom <= top)
        return (Rect){0, 0, 0, 0};
    return (Rect){.x = left, .y = top, .w = (right - left), .h = (bottom - top)};
}

Surface ZgfxSurfaceWrap(void *pixels, int w, int h, int stride, ColorFormat fmt)
{
    return (Surface){
        .pixels = pixels, .clip = (Rect){0, 0, w, h}, .fmt = fmt, .stride = stride, .w = w, .h = h};
}

Surface ZgfxSubSurface(const Surface *parent, Rect r)
{
    Rect bounds = {0, 0, parent->w, parent->h};
    Rect c = ZgfxRectIntersect(r, bounds);
    if (ZgfxRectIsEmpty(c))
        return (Surface){.clip = (Rect){0, 0, 0, 0},
                         .pixels = NULL,
                         .w = 0,
                         .h = 0,
                         .fmt = parent->fmt,
                         .stride = parent->stride};
    Surface subsurf = {0};
    subsurf.w = c.w;
    subsurf.h = c.h;
    subsurf.pixels =
        parent->pixels + (c.y * parent->stride) + (c.x * ZgfxBytesPerPixel(parent->fmt));
    subsurf.stride = parent->stride;
    subsurf.fmt = parent->fmt;
    subsurf.clip = (Rect){0, 0, c.w, c.h};
    return subsurf;
}

void ZgfxSetClip(Surface *s, Rect r) { s->clip = ZgfxRectIntersect(r, (Rect){0, 0, s->w, s->h}); }

void ZgfxPutPixel(Surface *s, int x, int y, Color c)
{
    if (x < s->clip.x || x >= s->clip.x + s->clip.w)
        return;
    if (y < s->clip.y || y >= s->clip.y + s->clip.h)
        return;
    uint32_t raw = ZgfxPack(s->fmt, c);
    uint8_t *p = s->pixels + (y * s->stride) + (x * ZgfxBytesPerPixel(s->fmt));
    switch (s->fmt) {
    case COLOR_FMT_RGB565:
        *(uint16_t *)p = (uint16_t)raw;
        break;
    case COLOR_FMT_XRGB8888:
        *(uint32_t *)p = raw;
        break;
    }
}

Color ZgfxGetPixel(const Surface *s, int x, int y)
{
    if (x < 0 || x >= s->w || y < 0 || y >= s->h)
        return 0;

    const uint8_t *p = s->pixels + (y * s->stride) + (x * ZgfxBytesPerPixel(s->fmt));
    switch (s->fmt) {
    case COLOR_FMT_RGB565:
        return ZgfxUnpack(s->fmt, *(const uint16_t *)p);
    case COLOR_FMT_XRGB8888:
        return ZgfxUnpack(s->fmt, *(const uint32_t *)p);
    }
    return 0;
}

void ZgfxFillRect(Surface *s, Rect r, Color c)
{
    Rect clipped = ZgfxRectIntersect(s->clip, r);
    if (ZgfxRectIsEmpty(clipped))
        return;

    uint32_t raw = ZgfxPack(s->fmt, c);
    int bpp = ZgfxBytesPerPixel(s->fmt);
    uint8_t *base = s->pixels + (clipped.y * s->stride) + (clipped.x * bpp);

    switch (s->fmt) {
    case COLOR_FMT_RGB565:
        for (int y = 0; y < clipped.h; y++) {
            uint16_t *row = (uint16_t *)(base + (y * s->stride));
            for (int x = 0; x < clipped.w; x++)
                row[x] = (uint16_t)raw;
        }
        break;
    case COLOR_FMT_XRGB8888:
        for (int y = 0; y < clipped.h; y++) {
            uint32_t *row = (uint32_t *)(base + (y * s->stride));
            for (int x = 0; x < clipped.w; x++)
                row[x] = raw;
        }
        break;
    }
}

void ZgfxHLine(Surface *s, int x, int y, int len, Color c)
{
    ZgfxFillRect(s, (Rect){x, y, len, 1}, c);
}

void ZgfxVLine(Surface *s, int x, int y, int len, Color c)
{
    ZgfxFillRect(s, (Rect){x, y, 1, len}, c);
}

void ZgfxDrawRect(Surface *s, Rect r, Color c)
{
    if (ZgfxRectIsEmpty(r))
        return;

    ZgfxHLine(s, r.x, r.y, r.w, c);
    if (r.h > 1)
        ZgfxHLine(s, r.x, r.y + r.h - 1, r.w, c);
    if (r.h > 2) {
        ZgfxVLine(s, r.x, r.y + 1, r.h - 2, c);
        if (r.w > 1)
            ZgfxVLine(s, r.x + r.w - 1, r.y + 1, r.h - 2, c);
    }
}

void ZgfxDrawLine(Surface *s, int x0, int y0, int x1, int y1, Color c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        ZgfxPutPixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
