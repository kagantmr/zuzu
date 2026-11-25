#include <dev/keymap.h>
#include <dev/tty.h>
#include <fs/fsd_client.h>
#include <fs/protocols/fsd.h>
#include <stdio.h>
#include <string.h>
#include <util/font12x16.h>
#include <util/spawn.h>
#include <util/vterm.h>
#include <zgfx/win.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "term"
#include <util/log.h>

#define COLS 84
#define ROWS 34
#define BIT_TTY 8
#define BIT_CHILD 9
#define WAKE_BITS (WM_EVT_ALL | (1U << BIT_TTY) | (1U << BIT_CHILD))
#define TERM_FG 0xC618U
#define TERM_BG 0x0000U
#define DEFAULT_PROGRAM "/bin/zzsh"
#define INPUT_BATCH 16
#define ENV_MAX 128

static WinConn g_conn;
static Win g_win;
static Handle g_event = -1;
static VTerm g_vt;
static Handle g_tty_port = -1;
static TtyConn g_tty;
static KeymapState g_keymap;
static char g_alias[TTY_NAME_MAX];
static Handle g_task = -1;
static Spid g_pid = -1;
static bool g_frame_pending;
static bool g_dirty;
static int32_t g_dx1;
static int32_t g_dy1;
static int32_t g_dx2;
static int32_t g_dy2;

static void Dirty(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (!g_dirty) {
        g_dx1 = x1;
        g_dy1 = y1;
        g_dx2 = x2;
        g_dy2 = y2;
        g_dirty = true;
        return;
    }
    g_dx1 = x1 < g_dx1 ? x1 : g_dx1;
    g_dy1 = y1 < g_dy1 ? y1 : g_dy1;
    g_dx2 = x2 > g_dx2 ? x2 : g_dx2;
    g_dy2 = y2 > g_dy2 ? y2 : g_dy2;
}

static void Present(void)
{
    if (!g_dirty || g_frame_pending)
        return;
    if (WinDamage(&g_win, g_dx1, g_dy1, (uint32_t)(g_dx2 - g_dx1 + 1),
                  (uint32_t)(g_dy2 - g_dy1 + 1)) == ZUZU_OK)
        g_frame_pending = true;
    g_dirty = false;
}

static void DrawCell(void *ctx, uint32_t col, uint32_t row, char ch, uint16_t fg, uint16_t bg)
{
    (void)ctx;
    uint32_t idx = (uint8_t)ch;
    if (idx < FONT_FIRST_CHAR || idx > FONT_LAST_CHAR)
        idx = ' ';
    const uint16_t *glyph = FONT12X16[idx - FONT_FIRST_CHAR];
    int32_t x = (int32_t)col * FONT_GLYPH_W;
    int32_t y = (int32_t)row * FONT_GLYPH_H;
    for (int32_t gy = 0; gy < FONT_GLYPH_H; gy++) {
        uint16_t *dst = g_win.px + ((size_t)(y + gy) * g_win.width) + x;
        uint16_t bits = glyph[gy];
        for (int32_t gx = 0; gx < FONT_GLYPH_W; gx++)
            dst[gx] = ((bits >> (FONT_GLYPH_W - 1 - gx)) & 1U) ? fg : bg;
    }
    Dirty(x, y, x + FONT_GLYPH_W - 1, y + FONT_GLYPH_H - 1);
}

static void ClearRow(void *ctx, uint32_t row, uint16_t color)
{
    (void)ctx;
    int32_t y = (int32_t)row * FONT_GLYPH_H;
    uint16_t *dst = g_win.px + ((size_t)y * g_win.width);
    for (size_t i = 0; i < (size_t)FONT_GLYPH_H * g_win.width; i++)
        dst[i] = color;
    Dirty(0, y, (int32_t)g_win.width - 1, y + FONT_GLYPH_H - 1);
}

static void ScrollUp(void *ctx, uint16_t color)
{
    (void)ctx;
    size_t line = (size_t)g_win.width * FONT_GLYPH_H;
    size_t total = (size_t)g_win.width * g_win.height;
    memmove(g_win.px, g_win.px + line, (total - line) * sizeof(uint16_t));
    for (size_t i = total - line; i < total; i++)
        g_win.px[i] = color;
    Dirty(0, 0, (int32_t)g_win.width - 1, (int32_t)g_win.height - 1);
}

static const VTermOps k_ops = {.draw_cell = DrawCell, .clear_row = ClearRow, .scroll_up = ScrollUp};

static void Print(const char *s)
{
    VTermShowCursor(&g_vt, false);
    while (*s)
        VTermFeed(&g_vt, (uint8_t)*s++);
    VTermShowCursor(&g_vt, true);
}

static void DrainTty(void)
{
    uint8_t buf[256];
    bool any = false;
    VTermShowCursor(&g_vt, false);
    for (;;) {
        uint32_t n = ShmRingPop(&g_tty.shm->down_hdr, g_tty.shm->down_data, buf, sizeof(buf));
        if (n == 0)
            break;
        for (uint32_t i = 0; i < n; i++)
            VTermFeed(&g_vt, buf[i]);
        any = true;
    }
    VTermShowCursor(&g_vt, true);
    if (any)
        Signal(g_tty.doorbell, 1U << g_tty.bit, false);
}

static void DrainKeys(void)
{
    InputEvent ev[INPUT_BATCH];
    uint32_t n;
    bool sent = false;
    while ((n = WinReadInput(&g_win, ev, INPUT_BATCH)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            if (ev[i].type != INPUT_EV_KEY)
                continue;
            uint8_t seq[KEYMAP_MAX_SEQ];
            size_t len = KeymapFeed(&g_keymap, (uint8_t)ev[i].code, ev[i].value != 0, seq,
                                    sizeof(seq));
            if (len > 0) {
                ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, seq, (uint32_t)len);
                sent = true;
            }
        }
    }
    if (sent)
        Signal(g_tty.doorbell, 1U << g_tty.bit, false);
}

static Err StartChild(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : DEFAULT_PROGRAM;
    FsdConn fsd;
    Err rc = FsdConnect(&fsd, FSD_SHM_DEFAULT);
    if (rc != ZUZU_OK)
        return rc;
    Handle image = -1;
    size_t size = 0;
    rc = FsdReadImage(&fsd, path, &image, &size);
    FsdDetach(&fsd);
    if (rc != ZUZU_OK)
        return rc;

    char args[256];
    size_t arglen = 0;
    uint32_t nargs = 0;
    for (int i = 1; i < argc || (i == 1 && argc <= 1); i++) {
        const char *a = argc > 1 ? argv[i] : DEFAULT_PROGRAM;
        size_t len = strlen(a) + 1;
        if (arglen + len > sizeof(args))
            break;
        memcpy(args + arglen, a, len);
        arglen += len;
        nargs++;
    }

    char env[ENV_MAX];
    size_t envlen = 0;
    const char *vars[] = {"TTY=", g_alias, NULL, "HOME=/", NULL, "PWD=/", NULL};
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        if (!vars[i]) {
            env[envlen++] = '\0';
            continue;
        }
        size_t len = strlen(vars[i]);
        memcpy(env + envlen, vars[i], len);
        envlen += len;
    }
    SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT,
                      .max_prio = SPAWN_PRIO_INHERIT,
                      .envbuf = env,
                      .envbuf_len = envlen,
                      .envc = 3};
    const char *name = strrchr(path, '/');
    rc = SpawnProcessObjAttr(image, size, name ? name + 1 : path, args, arglen, nargs, &attr,
                             &g_pid, &g_task);
    HandleClose(image);
    if (rc != ZUZU_OK)
        return rc;
    return Bind(EVENT_TASK, g_event, g_task, BIT_CHILD);
}

static void StopChild(void)
{
    if (g_task >= 0)
        HandleClose(g_task);
    if (g_pid >= 0)
        HandleDestroy((Handle)g_pid);
    g_task = -1;
    g_pid = -1;
}

static Err Setup(void)
{
    Err rc = WinConnect(&g_conn, 5000);
    if (rc != ZUZU_OK)
        return rc;
    WinSpec spec = {.width = COLS * FONT_GLYPH_W,
                    .height = ROWS * FONT_GLYPH_H,
                    .layer = WM_LAYER_NORMAL,
                    .title = "Terminal"};
    rc = WinCreate(&g_conn, &spec, g_event, 0, &g_win);
    if (rc != ZUZU_OK)
        return rc;
    snprintf(g_alias, sizeof(g_alias), "term%u", (unsigned)g_win.id);
    VTermInit(&g_vt, &k_ops, NULL, COLS, ROWS, TERM_FG, TERM_BG);
    KeymapInit(&g_keymap);

    g_tty_port = LookupService("/svc/tty");
    if (g_tty_port < 0)
        return ERR_NOENT;
    rc = TtyClientConnectDir(g_tty_port, TTY_PROVIDE, g_alias, TTY_DIR_BOTH | TTY_EP_EXPLICIT,
                             g_event, BIT_TTY, &g_tty);
    if (rc != ZUZU_OK)
        return rc;
    return TtyClientSetWinsz(g_tty_port, &g_tty, COLS, ROWS);
}

int main(int argc, char **argv)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return 1;
    Err rc = Setup();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "cannot open a terminal window: %s", StrToError(rc));
        return 1;
    }
    rc = StartChild(argc, argv);
    if (rc != ZUZU_OK) {
        Print("term: cannot start ");
        Print(argc > 1 ? argv[1] : DEFAULT_PROGRAM);
        Print(": ");
        Print(StrToError(rc));
        Print("\r\n");
    }
    Present();

    for (;;) {
        EventWaitResult w = WaitOnEvent(g_event, TIMEOUT_INFINITE, WAKE_BITS, false);
        uint32_t bits = w.status == ZUZU_OK ? w.bits : 0;
        if (bits & WM_EVT_CLOSE)
            break;
        if (bits & WM_EVT_FRAME)
            g_frame_pending = false;
        if (bits & WM_EVT_INPUT)
            DrainKeys();
        if (bits & (1U << BIT_TTY))
            DrainTty();
        if ((bits & (1U << BIT_CHILD)) && g_task >= 0) {
            DrainTty();
            StopChild();
            break;
        }
        Present();
    }
    StopChild();
    WinClose(&g_win);
    return 0;
}
