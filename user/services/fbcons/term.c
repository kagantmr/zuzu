#include "ansi.h"
#include "fbcons.h"
#include <string.h>

#define TAB_WIDTH 8

static uint32_t g_cols;
static uint32_t g_rows;
static uint32_t g_cx;
static uint32_t g_cy;
static char g_cells[MAX_ROWS][MAX_COLS];
static uint16_t g_cell_fg[MAX_ROWS][MAX_COLS];
static uint16_t g_cell_bg[MAX_ROWS][MAX_COLS];
static Ansi g_ansi;

typedef enum { PEN_DEFAULT, PEN_INDEX, PEN_RGB } PenKind;

typedef struct {
    PenKind kind;
    uint8_t index;
    uint16_t rgb;
} PenColor;

static PenColor g_pen_fg;
static PenColor g_pen_bg;
static bool g_bold;
static bool g_reverse;

static uint32_t Clamp(uint32_t v, uint32_t max) { return v > max ? max : v; }

static uint32_t Param(const uint32_t *p, uint32_t n, uint32_t i, uint32_t def)
{
    return (i < n && p[i] != 0) ? p[i] : def;
}

static uint16_t ResolveFg(void)
{
    switch (g_pen_fg.kind) {
    case PEN_INDEX:
        return ColorFromIndex((g_bold && g_pen_fg.index < 8) ? g_pen_fg.index + 8U : g_pen_fg.index);
    case PEN_RGB:
        return g_pen_fg.rgb;
    case PEN_DEFAULT:
        break;
    }
    return DEFAULT_FG;
}

static uint16_t ResolveBg(void)
{
    switch (g_pen_bg.kind) {
    case PEN_INDEX:
        return ColorFromIndex(g_pen_bg.index);
    case PEN_RGB:
        return g_pen_bg.rgb;
    case PEN_DEFAULT:
        break;
    }
    return DEFAULT_BG;
}

static void PenColors(uint16_t *fg, uint16_t *bg)
{
    *fg = ResolveFg();
    *bg = ResolveBg();
    if (g_reverse) {
        uint16_t t = *fg;
        *fg = *bg;
        *bg = t;
    }
}

static void FillRow(uint32_t row, uint32_t from, uint32_t to)
{
    uint16_t fg = ResolveFg();
    uint16_t bg = ResolveBg();
    for (uint32_t c = from; c < to; c++) {
        g_cells[row][c] = ' ';
        g_cell_fg[row][c] = fg;
        g_cell_bg[row][c] = bg;
    }
}

static void Scroll(void)
{
    DisplayScrollUp(ResolveBg());
    memmove(g_cells[0], g_cells[1], sizeof(g_cells[0]) * (g_rows - 1U));
    memmove(g_cell_fg[0], g_cell_fg[1], sizeof(g_cell_fg[0]) * (g_rows - 1U));
    memmove(g_cell_bg[0], g_cell_bg[1], sizeof(g_cell_bg[0]) * (g_rows - 1U));
    FillRow(g_rows - 1U, 0, g_cols);
}

static void LineFeed(void)
{
    if (g_cy + 1U >= g_rows)
        Scroll();
    else
        g_cy++;
}

static void EraseCells(uint32_t row, uint32_t from, uint32_t to)
{
    FillRow(row, from, to);
    if (from == 0 && to == g_cols) {
        DisplayClearRow(row, ResolveBg());
        return;
    }
    for (uint32_t c = from; c < to; c++)
        DisplayDrawCell(c, row, ' ', g_cell_fg[row][c], g_cell_bg[row][c]);
}

static void EraseDisplay(uint32_t mode)
{
    if (mode == 0) {
        EraseCells(g_cy, g_cx, g_cols);
        for (uint32_t r = g_cy + 1U; r < g_rows; r++)
            EraseCells(r, 0, g_cols);
    } else if (mode == 1) {
        for (uint32_t r = 0; r < g_cy; r++)
            EraseCells(r, 0, g_cols);
        EraseCells(g_cy, 0, g_cx + 1U);
    } else if (mode == 2) {
        for (uint32_t r = 0; r < g_rows; r++)
            EraseCells(r, 0, g_cols);
    }
}

static void EraseLine(uint32_t mode)
{
    if (mode == 0)
        EraseCells(g_cy, g_cx, g_cols);
    else if (mode == 1)
        EraseCells(g_cy, 0, g_cx + 1U);
    else if (mode == 2)
        EraseCells(g_cy, 0, g_cols);
}

static void PutChar(char ch)
{
    uint16_t fg;
    uint16_t bg;
    PenColors(&fg, &bg);
    g_cells[g_cy][g_cx] = ch;
    g_cell_fg[g_cy][g_cx] = fg;
    g_cell_bg[g_cy][g_cx] = bg;
    DisplayDrawCell(g_cx, g_cy, ch, fg, bg);
    if (++g_cx >= g_cols) {
        g_cx = 0;
        LineFeed();
    }
}

static void OnPrint(void *ctx, uint8_t ch)
{
    (void)ctx;
    if (DisplayPrintable(ch))
        PutChar((char)ch);
}

static void OnControl(void *ctx, uint8_t ch)
{
    (void)ctx;
    switch (ch) {
    case '\n':
        LineFeed();
        break;
    case '\r':
        g_cx = 0;
        break;
    case '\b':
    case 0x7F:
        if (g_cx > 0)
            g_cx--;
        break;
    case '\t':
        do {
            PutChar(' ');
        } while (g_cx % TAB_WIDTH != 0);
        break;
    default:
        break;
    }
}

static void PenReset(void)
{
    g_pen_fg = (PenColor){.kind = PEN_DEFAULT};
    g_pen_bg = (PenColor){.kind = PEN_DEFAULT};
    g_bold = false;
    g_reverse = false;
}

static uint32_t ExtendedColor(PenColor *pen, const uint32_t *p, uint32_t n, uint32_t i)
{
    if (i + 1 < n && p[i + 1] == 5 && i + 2 < n) {
        *pen = (PenColor){.kind = PEN_INDEX, .index = (uint8_t)p[i + 2]};
        return i + 2;
    }
    if (i + 1 < n && p[i + 1] == 2 && i + 4 < n) {
        *pen = (PenColor){.kind = PEN_RGB,
                          .rgb = ColorFromRgb((uint8_t)p[i + 2], (uint8_t)p[i + 3], (uint8_t)p[i + 4])};
        return i + 4;
    }
    return n;
}

static void Sgr(const uint32_t *p, uint32_t n)
{
    if (n == 0) {
        PenReset();
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = p[i];
        if (v == 0)
            PenReset();
        else if (v == 1)
            g_bold = true;
        else if (v == 22)
            g_bold = false;
        else if (v == 7)
            g_reverse = true;
        else if (v == 27)
            g_reverse = false;
        else if (v >= 30 && v <= 37)
            g_pen_fg = (PenColor){.kind = PEN_INDEX, .index = (uint8_t)(v - 30)};
        else if (v == 38)
            i = ExtendedColor(&g_pen_fg, p, n, i);
        else if (v == 39)
            g_pen_fg = (PenColor){.kind = PEN_DEFAULT};
        else if (v >= 40 && v <= 47)
            g_pen_bg = (PenColor){.kind = PEN_INDEX, .index = (uint8_t)(v - 40)};
        else if (v == 48)
            i = ExtendedColor(&g_pen_bg, p, n, i);
        else if (v == 49)
            g_pen_bg = (PenColor){.kind = PEN_DEFAULT};
        else if (v >= 90 && v <= 97)
            g_pen_fg = (PenColor){.kind = PEN_INDEX, .index = (uint8_t)(v - 90 + 8)};
        else if (v >= 100 && v <= 107)
            g_pen_bg = (PenColor){.kind = PEN_INDEX, .index = (uint8_t)(v - 100 + 8)};
    }
}

static void OnCsi(void *ctx, char prefix, char final, const uint32_t *p, uint32_t n)
{
    (void)ctx;
    if (prefix != 0)
        return;

    switch (final) {
    case 'H':
    case 'f':
        g_cy = Clamp(Param(p, n, 0, 1) - 1U, g_rows - 1U);
        g_cx = Clamp(Param(p, n, 1, 1) - 1U, g_cols - 1U);
        break;
    case 'A':
        g_cy -= Clamp(Param(p, n, 0, 1), g_cy);
        break;
    case 'B':
        g_cy = Clamp(g_cy + Param(p, n, 0, 1), g_rows - 1U);
        break;
    case 'C':
        g_cx = Clamp(g_cx + Param(p, n, 0, 1), g_cols - 1U);
        break;
    case 'D':
        g_cx -= Clamp(Param(p, n, 0, 1), g_cx);
        break;
    case 'J':
        EraseDisplay(n > 0 ? p[0] : 0);
        break;
    case 'K':
        EraseLine(n > 0 ? p[0] : 0);
        break;
    case 'm':
        Sgr(p, n);
        break;
    default:
        break;
    }
}

static const AnsiHandlers g_handlers = {.print = OnPrint, .control = OnControl, .csi = OnCsi};

void TermInit(void)
{
    g_cols = DisplayCols();
    g_rows = DisplayRows();
    g_cx = 0;
    g_cy = 0;
    PenReset();
    AnsiInit(&g_ansi, &g_handlers, NULL);
    EraseDisplay(2);
}

void TermShowCursor(bool on)
{
    uint16_t fg = g_cell_fg[g_cy][g_cx];
    uint16_t bg = g_cell_bg[g_cy][g_cx];
    if (on)
        DisplayDrawCell(g_cx, g_cy, g_cells[g_cy][g_cx], bg, fg);
    else
        DisplayDrawCell(g_cx, g_cy, g_cells[g_cy][g_cx], fg, bg);
}

void TermFeed(uint8_t b) { AnsiFeed(&g_ansi, b); }
