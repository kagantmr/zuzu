#include "font16x16.h"
#include <dev/protocols/fb.h>
#include <dev/tty.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "fbcons"
#include <util/log.h>

#define BIT_KICK 2
#define MASK(bit) (1U << (bit))
#define MAX_COLS 128
#define MAX_ROWS 64
#define TAB_WIDTH 8
#define COLOR_FG 0xC618U
#define COLOR_BG 0x0000U
#define ALIAS "fbcons"

typedef enum { ST_TEXT, ST_ESC, ST_CSI } ParseState;

static Handle g_gfx = -1;
static Handle g_event = -1;
static uint32_t g_token;
static FbInfo g_info;
static uint8_t *g_fb;
static TtyConn g_tty;

static uint32_t g_cols;
static uint32_t g_rows;
static uint32_t g_cx;
static uint32_t g_cy;
static char g_cells[MAX_ROWS][MAX_COLS];

static ParseState g_state = ST_TEXT;
static uint32_t g_param;
static bool g_have_param;

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

static void DrawCell(uint32_t col, uint32_t row, char ch, bool invert)
{
    uint32_t idx = (uint8_t)ch;
    if (idx < FONT_FIRST_CHAR || idx > FONT_LAST_CHAR)
        idx = ' ';
    const uint16_t *glyph = font16x16[idx - FONT_FIRST_CHAR];
    uint16_t fg = invert ? COLOR_BG : COLOR_FG;
    uint16_t bg = invert ? COLOR_FG : COLOR_BG;

    for (uint32_t gy = 0; gy < FONT_GLYPH_H; gy++) {
        uint16_t *dst = Pixel(col * FONT_GLYPH_W, (row * FONT_GLYPH_H) + gy);
        uint16_t bits = glyph[gy];
        for (uint32_t gx = 0; gx < FONT_GLYPH_W; gx++)
            dst[gx] = ((bits >> (FONT_GLYPH_W - 1U - gx)) & 1U) ? fg : bg;
    }
}

static void ShowCursor(bool on) { DrawCell(g_cx, g_cy, g_cells[g_cy][g_cx], on); }

static void ClearRowPixels(uint32_t row)
{
    for (uint32_t gy = 0; gy < FONT_GLYPH_H; gy++) {
        uint16_t *dst = Pixel(0, (row * FONT_GLYPH_H) + gy);
        for (uint32_t x = 0; x < g_cols * FONT_GLYPH_W; x++)
            dst[x] = COLOR_BG;
    }
}

static void Scroll(void)
{
    uint32_t line_bytes = g_info.stride * FONT_GLYPH_H;
    memmove(g_fb, g_fb + line_bytes, line_bytes * (g_rows - 1U));
    memmove(g_cells[0], g_cells[1], sizeof(g_cells[0]) * (g_rows - 1U));
    memset(g_cells[g_rows - 1U], ' ', g_cols);
    ClearRowPixels(g_rows - 1U);
}

static void LineFeed(void)
{
    if (g_cy + 1U >= g_rows)
        Scroll();
    else
        g_cy++;
}

static void ClearScreen(void)
{
    for (uint32_t r = 0; r < g_rows; r++) {
        memset(g_cells[r], ' ', g_cols);
        ClearRowPixels(r);
    }
    g_cx = 0;
    g_cy = 0;
}

static void ClearToEol(void)
{
    for (uint32_t c = g_cx; c < g_cols; c++) {
        g_cells[g_cy][c] = ' ';
        DrawCell(c, g_cy, ' ', false);
    }
}

static void PutChar(char ch)
{
    g_cells[g_cy][g_cx] = ch;
    DrawCell(g_cx, g_cy, ch, false);
    if (++g_cx >= g_cols) {
        g_cx = 0;
        LineFeed();
    }
}

static void CsiFinal(char final)
{
    switch (final) {
    case 'H':
    case 'f':
        g_cx = 0;
        g_cy = 0;
        break;
    case 'J':
        if (g_have_param && g_param == 2)
            ClearScreen();
        break;
    case 'K':
        ClearToEol();
        break;
    default:
        break;
    }
}

static void Feed(uint8_t b)
{
    switch (g_state) {
    case ST_ESC:
        if (b == '[') {
            g_state = ST_CSI;
            g_param = 0;
            g_have_param = false;
        } else {
            g_state = ST_TEXT;
        }
        return;
    case ST_CSI:
        if (b >= '0' && b <= '9') {
            g_param = (g_param * 10U) + (b - '0');
            g_have_param = true;
        } else if (b == ';') {
            g_param = 0;
        } else if (b >= 0x40 && b <= 0x7E) {
            CsiFinal((char)b);
            g_state = ST_TEXT;
        }
        return;
    case ST_TEXT:
        break;
    }

    switch (b) {
    case 0x1B:
        g_state = ST_ESC;
        break;
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
        if (b >= FONT_FIRST_CHAR && b <= FONT_LAST_CHAR)
            PutChar((char)b);
        break;
    }
}

static bool Drain(void)
{
    uint8_t buf[64];
    bool any = false;
    ShmRingHdr *hdr = &g_tty.shm->down_hdr;

    ShowCursor(false);
    for (;;) {
        uint32_t n = ShmRingPop(hdr, g_tty.shm->down_data, buf, sizeof(buf));
        if (n == 0)
            break;
        for (uint32_t i = 0; i < n; i++)
            Feed(buf[i]);
        any = true;
    }
    ShowCursor(true);
    return any;
}

static Err SetupDisplay(void)
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

static Err SetupTty(void)
{
    Handle tty_port = -1;
    while (tty_port < 0) {
        tty_port = LookupService("/svc/tty");
        if (tty_port < 0)
            Sleep(10);
    }
    return TtyClientConnect(tty_port, TTY_PROVIDE, ALIAS, g_event, BIT_KICK, &g_tty);
}

int main(void)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return 1;

    Err rc = SetupDisplay();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "display unavailable rc=%d", rc);
        return 1;
    }

    ClearScreen();
    ShowCursor(true);

    rc = SetupTty();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "tty provide rc=%d", rc);
        return 1;
    }

    Handle ready = CreatePort();
    if (ready >= 0)
        RegisterService("/svc/fbcons", ready);

    for (;;) {
        WaitOn(g_event, TIMEOUT_INFINITE);
        if (Drain())
            Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    }
}
