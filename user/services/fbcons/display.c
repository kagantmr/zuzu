#include "fbcons.h"
#include "font16x16.h"
#include <dev/protocols/fb.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

static Handle g_gfx = -1;
static uint32_t g_token;
static FbInfo g_info;
static uint8_t *g_fb;
static uint32_t g_cols;
static uint32_t g_rows;

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

static inline uint16_t *Pixel(uint32_t x, uint32_t y)
{
    return (uint16_t *)(g_fb + (y * g_info.stride) + (x * 2U));
}

uint32_t DisplayCols(void) { return g_cols; }

uint32_t DisplayRows(void) { return g_rows; }

bool DisplayPrintable(uint8_t b) { return b >= FONT_FIRST_CHAR && b <= FONT_LAST_CHAR; }

void DisplayDrawCell(uint32_t col, uint32_t row, char ch, uint16_t fg, uint16_t bg)
{
    uint32_t idx = (uint8_t)ch;
    if (!DisplayPrintable((uint8_t)idx))
        idx = ' ';
    const uint16_t *glyph = FONT16X16[idx - FONT_FIRST_CHAR];

    for (uint32_t gy = 0; gy < FONT_GLYPH_H; gy++) {
        uint16_t *dst = Pixel(col * FONT_GLYPH_W, (row * FONT_GLYPH_H) + gy);
        uint16_t bits = glyph[gy];
        for (uint32_t gx = 0; gx < FONT_GLYPH_W; gx++)
            dst[gx] = ((bits >> (FONT_GLYPH_W - 1U - gx)) & 1U) ? fg : bg;
    }
}

void DisplayClearRow(uint32_t row, uint16_t color)
{
    for (uint32_t gy = 0; gy < FONT_GLYPH_H; gy++) {
        uint16_t *dst = Pixel(0, (row * FONT_GLYPH_H) + gy);
        for (uint32_t x = 0; x < g_cols * FONT_GLYPH_W; x++)
            dst[x] = color;
    }
}

void DisplayScrollUp(uint16_t color)
{
    uint32_t line_bytes = g_info.stride * FONT_GLYPH_H;
    memmove(g_fb, g_fb + line_bytes, line_bytes * (g_rows - 1U));
    DisplayClearRow(g_rows - 1U, color);
}

Err DisplayInit(void)
{
    for (int i = 0; i < 500 && g_gfx < 0; i++) {
        g_gfx = LookupService("/svc/gfx");
        if (g_gfx < 0)
            Sleep(10);
    }
    if (g_gfx < 0)
        return ERR_NOENT;

    Err rc = ERR_NOENT;
    for (int i = 0; i < 500; i++) {
        rc = FbCall(FB_CMD_GET_INFO, 0, -1, &g_info, sizeof(g_info), NULL);
        if (rc != ERR_NOENT)
            break;
        Sleep(10);
    }
    if (rc != ZUZU_OK)
        return rc;
    if (g_info.format != FB_FMT_RGB565)
        return ERR_NOSYS;

    Handle live = CreatePort();
    Handle dup;
    Handle mem = -1;
    rc = HandleDup(live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    rc = FbCall(FB_CMD_GET_BUF, 0, dup, &g_info, sizeof(g_info), &mem);
    HandleClose(dup);
    if (rc != ZUZU_OK)
        return rc;
    if (mem < 0)
        return ERR_MALFORMED;
    g_token = g_info.token;

    g_fb = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(g_fb))
        return (Err)(intptr_t)g_fb;

    g_cols = g_info.width / FONT_GLYPH_W;
    g_rows = g_info.height / FONT_GLYPH_H;
    if (g_cols > MAX_COLS)
        g_cols = MAX_COLS;
    if (g_rows > MAX_ROWS)
        g_rows = MAX_ROWS;
    if (g_cols == 0 || g_rows == 0)
        return ERR_BADARG;
    return ZUZU_OK;
}
