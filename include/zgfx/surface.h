#ifndef ZGFX_SURFACE_H
#define ZGFX_SURFACE_H

typedef uint32_t Color;
typedef uint8_t UByte;
typedef uint8_t Byte;

typedef enum {
    COLOR_FMT_RGB565,
    COLOR_FMT_XRGB8888
} ColorFormat;

typedef struct {
    int x, y;
    size_t w, h;
} Rect;

typedef struct {
    Byte *pixels;
    size_t w, h;
    size_t stride;
    ColorFormat fmt;
    int clip;
} Surface;

static inline int BytesPerPixel(ColorFormat fmt) {
    switch (fmt) {
        case COLOR_FMT_RGB565: return 2;
        case COLOR_FMT_XRGB8888: return 4;
        default: return (-1);
    }
}

static inline int      GfxBytesPerPixel(ColorFormat fmt);

static inline Color    GfxRgb(uint8_t r, uint8_t g, uint8_t b);
static inline Color    GfxArgb(uint8_t a, uint8_t r, uint8_t g, uint8_t b);

static inline uint32_t GfxPack(ColorFormat fmt, Color c);
static inline Color    GfxUnpack(ColorFormat fmt, uint32_t raw);

Rect GfxRectIntersect(Rect a, Rect b);
bool GfxRectIsEmpty(Rect r);

Surface GfxSurfaceWrap(void *pixels, int w, int h, int stride, ColorFormat fmt);
Surface GfxSubSurface(const Surface *parent, Rect r);
void    GfxSetClip(Surface *s, Rect r);

void  GfxPutPixel(Surface *s, int x, int y, Color c);
Color GfxGetPixel(const Surface *s, int x, int y);

#endif /* ZGFX_SURFACE_H */