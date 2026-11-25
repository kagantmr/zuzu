/**
 * @file
 * @brief Character-cell terminal emulator drawn through VTermOps callbacks.
 */

#include <string.h>
#include <util/vterm.h>

/** @brief Columns between tab stops. */
#define TAB_WIDTH 8

/** @brief RGB of the 16 standard ANSI colors. */
static const uint8_t k_ansi16[16][3] = {
    {0, 0, 0},       {170, 0, 0},     {0, 170, 0},     {170, 85, 0},
    {0, 0, 170},     {170, 0, 170},   {0, 170, 170},   {170, 170, 170},
    {85, 85, 85},    {255, 85, 85},   {85, 255, 85},   {255, 255, 85},
    {85, 85, 255},   {255, 85, 255},  {85, 255, 255},  {255, 255, 255},
};

/** @brief Channel levels of the 6x6x6 color cube in the 256-color palette. */
static const uint8_t k_cube[6] = {0, 95, 135, 175, 215, 255};

uint16_t VTermRgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t VTermPaletteColor(uint8_t index)
{
    if (index < 16)
        return VTermRgb(k_ansi16[index][0], k_ansi16[index][1], k_ansi16[index][2]);
    if (index < 232) {
        uint8_t v = index - 16;
        return VTermRgb(k_cube[v / 36], k_cube[(v / 6) % 6], k_cube[v % 6]);
    }
    uint8_t level = 8 + ((index - 232) * 10);
    return VTermRgb(level, level, level);
}

/**
 * @brief Limits @p v to @p max.
 *
 * @param[in] v   Value.
 * @param[in] max Upper bound.
 * @return The smaller of the two.
 */
static uint32_t Clamp(uint32_t v, uint32_t max) { return v > max ? max : v; }

/**
 * @brief Reads CSI parameter @p i, treating missing and zero as @p def.
 *
 * @param[in] p   Parameters.
 * @param[in] n   Parameter count.
 * @param[in] i   Index.
 * @param[in] def Default.
 * @return The parameter or @p def.
 */
static uint32_t Param(const uint32_t *p, uint32_t n, uint32_t i, uint32_t def)
{
    return (i < n && p[i] != 0) ? p[i] : def;
}

/**
 * @brief Turns the foreground pen into a color.
 *
 * @param[in] t Terminal.
 * @return RGB565 color.
 */
static uint16_t ResolveFg(const VTerm *t)
{
    switch (t->pen_fg.kind) {
    case VTERM_PEN_INDEX:
        return VTermPaletteColor((t->bold && t->pen_fg.index < 8) ? t->pen_fg.index + 8U
                                                                  : t->pen_fg.index);
    case VTERM_PEN_RGB:
        return t->pen_fg.rgb;
    case VTERM_PEN_DEFAULT:
        break;
    }
    return t->default_fg;
}

/**
 * @brief Turns the background pen into a color.
 *
 * @param[in] t Terminal.
 * @return RGB565 color.
 */
static uint16_t ResolveBg(const VTerm *t)
{
    switch (t->pen_bg.kind) {
    case VTERM_PEN_INDEX:
        return VTermPaletteColor(t->pen_bg.index);
    case VTERM_PEN_RGB:
        return t->pen_bg.rgb;
    case VTERM_PEN_DEFAULT:
        break;
    }
    return t->default_bg;
}

/**
 * @brief Colors to draw a cell with, after bold and reverse.
 *
 * @param[in]  t  Terminal.
 * @param[out] fg Foreground.
 * @param[out] bg Background.
 */
static void PenColors(const VTerm *t, uint16_t *fg, uint16_t *bg)
{
    *fg = ResolveFg(t);
    *bg = ResolveBg(t);
    if (t->reverse) {
        uint16_t tmp = *fg;
        *fg = *bg;
        *bg = tmp;
    }
}

/**
 * @brief Clears columns @p from to @p to - 1 of @p row in the background color.
 *
 * @param[in,out] t    Terminal.
 * @param[in]     row  Row.
 * @param[in]     from First column.
 * @param[in]     to   Column after the last.
 */
static void FillRow(VTerm *t, uint32_t row, uint32_t from, uint32_t to)
{
    uint16_t fg = ResolveFg(t);
    uint16_t bg = ResolveBg(t);
    for (uint32_t c = from; c < to; c++) {
        t->cells[row][c] = ' ';
        t->fg[row][c] = fg;
        t->bg[row][c] = bg;
    }
}

/**
 * @brief Scrolls the screen up one row.
 *
 * @param[in,out] t Terminal.
 */
static void Scroll(VTerm *t)
{
    t->ops->scroll_up(t->ctx, ResolveBg(t));
    memmove(t->cells[0], t->cells[1], sizeof(t->cells[0]) * (t->rows - 1U));
    memmove(t->fg[0], t->fg[1], sizeof(t->fg[0]) * (t->rows - 1U));
    memmove(t->bg[0], t->bg[1], sizeof(t->bg[0]) * (t->rows - 1U));
    FillRow(t, t->rows - 1U, 0, t->cols);
}

/**
 * @brief Moves the cursor down a row, scrolling at the bottom.
 *
 * @param[in,out] t Terminal.
 */
static void LineFeed(VTerm *t)
{
    if (t->cy + 1U >= t->rows)
        Scroll(t);
    else
        t->cy++;
}

/**
 * @brief Blanks columns @p from to @p to - 1 of @p row.
 *
 * @param[in,out] t    Terminal.
 * @param[in]     row  Row.
 * @param[in]     from First column.
 * @param[in]     to   Column after the last.
 */
static void EraseCells(VTerm *t, uint32_t row, uint32_t from, uint32_t to)
{
    FillRow(t, row, from, to);
    if (from == 0 && to == t->cols) {
        t->ops->clear_row(t->ctx, row, ResolveBg(t));
        return;
    }
    for (uint32_t c = from; c < to; c++)
        t->ops->draw_cell(t->ctx, c, row, ' ', t->fg[row][c], t->bg[row][c]);
}

/**
 * @brief ED: erases below, above or all of the screen.
 *
 * @param[in,out] t    Terminal.
 * @param[in]     mode 0 below, 1 above, 2 or 3 all.
 */
static void EraseDisplay(VTerm *t, uint32_t mode)
{
    if (mode == 0) {
        EraseCells(t, t->cy, t->cx, t->cols);
        for (uint32_t r = t->cy + 1U; r < t->rows; r++)
            EraseCells(t, r, 0, t->cols);
    } else if (mode == 1) {
        for (uint32_t r = 0; r < t->cy; r++)
            EraseCells(t, r, 0, t->cols);
        EraseCells(t, t->cy, 0, t->cx + 1U);
    } else if (mode == 2) {
        for (uint32_t r = 0; r < t->rows; r++)
            EraseCells(t, r, 0, t->cols);
    }
}

/**
 * @brief EL: erases right of, left of or all of the cursor's row.
 *
 * @param[in,out] t    Terminal.
 * @param[in]     mode 0 right, 1 left, 2 all.
 */
static void EraseLine(VTerm *t, uint32_t mode)
{
    if (mode == 0)
        EraseCells(t, t->cy, t->cx, t->cols);
    else if (mode == 1)
        EraseCells(t, t->cy, 0, t->cx + 1U);
    else if (mode == 2)
        EraseCells(t, t->cy, 0, t->cols);
}

/**
 * @brief Draws @p ch at the cursor and advances it, wrapping at the right edge.
 *
 * @param[in,out] t  Terminal.
 * @param[in]     ch Character.
 */
static void PutChar(VTerm *t, char ch)
{
    uint16_t fg;
    uint16_t bg;
    if (t->wrap_pending) {
        t->wrap_pending = false;
        t->cx = 0;
        LineFeed(t);
    }
    PenColors(t, &fg, &bg);
    t->cells[t->cy][t->cx] = ch;
    t->fg[t->cy][t->cx] = fg;
    t->bg[t->cy][t->cx] = bg;
    t->ops->draw_cell(t->ctx, t->cx, t->cy, ch, fg, bg);
    if (t->cx + 1U >= t->cols)
        t->wrap_pending = true;
    else
        t->cx++;
}

/**
 * @brief Parser callback for a printable byte.
 *
 * @param[in] ctx Terminal.
 * @param[in] ch  Byte.
 */
static void OnPrint(void *ctx, uint8_t ch)
{
    VTerm *t = ctx;
    if (ch >= 0x20 && ch < 0x7F)
        PutChar(t, (char)ch);
}

/**
 * @brief Parser callback for a C0 control byte.
 *
 * @param[in] ctx Terminal.
 * @param[in] ch  Byte.
 */
static void OnControl(void *ctx, uint8_t ch)
{
    VTerm *t = ctx;
    t->wrap_pending = false;
    switch (ch) {
    case '\n':
        LineFeed(t);
        break;
    case '\r':
        t->cx = 0;
        break;
    case '\b':
    case 0x7F:
        if (t->cx > 0)
            t->cx--;
        break;
    case '\t':
        do {
            PutChar(t, ' ');
        } while (t->cx % TAB_WIDTH != 0);
        break;
    default:
        break;
    }
}

/**
 * @brief SGR 0: default colors, no bold, no reverse.
 *
 * @param[in,out] t Terminal.
 */
static void PenReset(VTerm *t)
{
    t->pen_fg = (VTermPen){.kind = VTERM_PEN_DEFAULT};
    t->pen_bg = (VTermPen){.kind = VTERM_PEN_DEFAULT};
    t->bold = false;
    t->reverse = false;
}

/**
 * @brief Parses an SGR 38/48 color (5;n or 2;r;g;b) starting at @p i.
 *
 * @param[out] pen Pen to set.
 * @param[in]  p   Parameters.
 * @param[in]  n   Parameter count.
 * @param[in]  i   Index of the 38 or 48.
 * @return Index of the last parameter used, or @p n if malformed.
 */
static uint32_t ExtendedColor(VTermPen *pen, const uint32_t *p, uint32_t n, uint32_t i)
{
    if (i + 1 < n && p[i + 1] == 5 && i + 2 < n) {
        *pen = (VTermPen){.kind = VTERM_PEN_INDEX, .index = (uint8_t)p[i + 2]};
        return i + 2;
    }
    if (i + 1 < n && p[i + 1] == 2 && i + 4 < n) {
        *pen = (VTermPen){.kind = VTERM_PEN_RGB,
                          .rgb = VTermRgb((uint8_t)p[i + 2], (uint8_t)p[i + 3], (uint8_t)p[i + 4])};
        return i + 4;
    }
    return n;
}

/**
 * @brief Applies an SGR sequence.
 *
 * @param[in,out] t Terminal.
 * @param[in]     p Parameters.
 * @param[in]     n Parameter count.
 */
static void Sgr(VTerm *t, const uint32_t *p, uint32_t n)
{
    if (n == 0) {
        PenReset(t);
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = p[i];
        if (v == 0)
            PenReset(t);
        else if (v == 1)
            t->bold = true;
        else if (v == 22)
            t->bold = false;
        else if (v == 7)
            t->reverse = true;
        else if (v == 27)
            t->reverse = false;
        else if (v >= 30 && v <= 37)
            t->pen_fg = (VTermPen){.kind = VTERM_PEN_INDEX, .index = (uint8_t)(v - 30)};
        else if (v == 38)
            i = ExtendedColor(&t->pen_fg, p, n, i);
        else if (v == 39)
            t->pen_fg = (VTermPen){.kind = VTERM_PEN_DEFAULT};
        else if (v >= 40 && v <= 47)
            t->pen_bg = (VTermPen){.kind = VTERM_PEN_INDEX, .index = (uint8_t)(v - 40)};
        else if (v == 48)
            i = ExtendedColor(&t->pen_bg, p, n, i);
        else if (v == 49)
            t->pen_bg = (VTermPen){.kind = VTERM_PEN_DEFAULT};
        else if (v >= 90 && v <= 97)
            t->pen_fg = (VTermPen){.kind = VTERM_PEN_INDEX, .index = (uint8_t)(v - 90 + 8)};
        else if (v >= 100 && v <= 107)
            t->pen_bg = (VTermPen){.kind = VTERM_PEN_INDEX, .index = (uint8_t)(v - 100 + 8)};
    }
}

/**
 * @brief Parser callback for a complete CSI sequence.
 *
 * @param[in] ctx    Terminal.
 * @param[in] prefix Private prefix byte, or 0.
 * @param[in] final  Final byte.
 * @param[in] p      Parameters.
 * @param[in] n      Parameter count.
 */
static void OnCsi(void *ctx, char prefix, char final, const uint32_t *p, uint32_t n)
{
    VTerm *t = ctx;
    if (prefix != 0)
        return;
    if (final != 'm')
        t->wrap_pending = false;

    switch (final) {
    case 'H':
    case 'f':
        t->cy = Clamp(Param(p, n, 0, 1) - 1U, t->rows - 1U);
        t->cx = Clamp(Param(p, n, 1, 1) - 1U, t->cols - 1U);
        break;
    case 'A':
        t->cy -= Clamp(Param(p, n, 0, 1), t->cy);
        break;
    case 'B':
        t->cy = Clamp(t->cy + Param(p, n, 0, 1), t->rows - 1U);
        break;
    case 'C':
        t->cx = Clamp(t->cx + Param(p, n, 0, 1), t->cols - 1U);
        break;
    case 'D':
        t->cx -= Clamp(Param(p, n, 0, 1), t->cx);
        break;
    case 'J':
        EraseDisplay(t, n > 0 ? p[0] : 0);
        break;
    case 'K':
        EraseLine(t, n > 0 ? p[0] : 0);
        break;
    case 'm':
        Sgr(t, p, n);
        break;
    default:
        break;
    }
}

/** @brief Parser callbacks into the terminal. */
static const AnsiHandlers g_handlers = {.print = OnPrint, .control = OnControl, .csi = OnCsi};

void VTermInit(VTerm *t, const VTermOps *ops, void *ctx, uint32_t cols, uint32_t rows, uint16_t fg,
               uint16_t bg)
{
    t->ops = ops;
    t->ctx = ctx;
    t->cols = cols > VTERM_MAX_COLS ? VTERM_MAX_COLS : cols;
    t->rows = rows > VTERM_MAX_ROWS ? VTERM_MAX_ROWS : rows;
    t->cx = 0;
    t->cy = 0;
    t->wrap_pending = false;
    t->default_fg = fg;
    t->default_bg = bg;
    PenReset(t);
    AnsiInit(&t->ansi, &g_handlers, t);
    EraseDisplay(t, 2);
}

void VTermFeed(VTerm *t, uint8_t b) { AnsiFeed(&t->ansi, b); }

void VTermShowCursor(VTerm *t, bool on)
{
    uint16_t fg = t->fg[t->cy][t->cx];
    uint16_t bg = t->bg[t->cy][t->cx];
    if (on)
        t->ops->draw_cell(t->ctx, t->cx, t->cy, t->cells[t->cy][t->cx], bg, fg);
    else
        t->ops->draw_cell(t->ctx, t->cx, t->cy, t->cells[t->cy][t->cx], fg, bg);
}

void VTermRepaint(VTerm *t, bool cursor)
{
    for (uint32_t row = 0; row < t->rows; row++) {
        for (uint32_t col = 0; col < t->cols; col++)
            t->ops->draw_cell(t->ctx, col, row, t->cells[row][col], t->fg[row][col],
                              t->bg[row][col]);
    }
    if (cursor)
        VTermShowCursor(t, true);
}
