#ifndef ZGFX_SURFACE_H
#define ZGFX_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t Color;

typedef enum { COLOR_FMT_RGB565, COLOR_FMT_XRGB8888 } ColorFormat;

typedef struct {
    int x, y;
    int w, h;
} Rect;

typedef struct {
    uint8_t *pixels;
    int w, h;
    int stride;
    ColorFormat fmt;
    Rect clip;
} Surface;

static inline int ZgfxBytesPerPixel(ColorFormat fmt)
{
    switch (fmt) {
    case COLOR_FMT_RGB565:
        return 2;
    case COLOR_FMT_XRGB8888:
        return 4;
    }
    return -1;
}

static inline Color ZgfxArgb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline Color ZgfxRgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ZgfxArgb(0xFF, r, g, b);
}

static inline uint32_t ZgfxPack(ColorFormat fmt, Color c)
{
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;

    switch (fmt) {
    case COLOR_FMT_RGB565:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case COLOR_FMT_XRGB8888:
        return 0xFF000000U | (r << 16) | (g << 8) | b;
    }
    return 0;
}

static inline Color ZgfxUnpack(ColorFormat fmt, uint32_t raw)
{
    switch (fmt) {
    case COLOR_FMT_RGB565: {
        uint32_t r = (raw >> 11) & 0x1F;
        uint32_t g = (raw >> 5) & 0x3F;
        uint32_t b = raw & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000U | (r << 16) | (g << 8) | b;
    }
    case COLOR_FMT_XRGB8888:
        return 0xFF000000U | (raw & 0x00FFFFFFU);
    }
    return 0;
}

Rect ZgfxRectIntersect(Rect a, Rect b);
bool ZgfxRectIsEmpty(Rect r);

Surface ZgfxSurfaceWrap(void *pixels, int w, int h, int stride, ColorFormat fmt);
Surface ZgfxSubSurface(const Surface *parent, Rect r);
void ZgfxSetClip(Surface *s, Rect r);

void ZgfxPutPixel(Surface *s, int x, int y, Color c);
Color ZgfxGetPixel(const Surface *s, int x, int y);
void ZgfxFillRect(Surface *s, Rect r, Color c);
void ZgfxHLine(Surface *s, int x, int y, int len, Color c);
void ZgfxVLine(Surface *s, int x, int y, int len, Color c);
void ZgfxDrawRect(Surface *s, Rect r, Color c);
void ZgfxDrawLine(Surface *s, int x0, int y0, int x1, int y1, Color c);

#endif /* ZGFX_SURFACE_H */
