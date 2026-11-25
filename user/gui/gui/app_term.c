#include "gui.h"

#include <dev/keymap.h>
#include <dev/tty.h>
#include <fs/fsd_client.h>
#include <fs/protocols/fsd.h>
#include <stdlib.h>
#include <string.h>
#include <util/font12x16.h>
#include <util/spawn.h>
#include <util/vterm.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define TTY_ALIAS "gui"
#define TOOLBAR_H 40
#define MAX_ARGS 8
#define CMD_MAX 96
#define DEFAULT_CMD "kilo /notes.txt"
#define TERM_FG 0xC618U
#define TERM_BG 0x0000U

static VTerm g_vt;
static uint16_t *g_px;
static int32_t g_w;
static int32_t g_h;
static bool g_ready;

static Handle g_tty_port = -1;
static TtyConn g_tty;
static bool g_tty_up;
static FsdConn g_fsd;
static bool g_fsd_up;
static KeymapState g_keymap;

static Handle g_task = -1;
static bool g_running;

static lv_obj_t *g_canvas;
static lv_obj_t *g_cmd;
static lv_obj_t *g_run;
static lv_area_t g_dirty;
static bool g_has_dirty;

static void Dirty(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (!g_has_dirty) {
        g_dirty = (lv_area_t){.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2};
        g_has_dirty = true;
        return;
    }
    g_dirty.x1 = LV_MIN(g_dirty.x1, x1);
    g_dirty.y1 = LV_MIN(g_dirty.y1, y1);
    g_dirty.x2 = LV_MAX(g_dirty.x2, x2);
    g_dirty.y2 = LV_MAX(g_dirty.y2, y2);
}

static void FlushDirty(void)
{
    if (!g_has_dirty)
        return;
    g_has_dirty = false;
    if (!g_canvas)
        return;
    lv_area_t box;
    lv_obj_get_coords(g_canvas, &box);
    lv_area_t a = {.x1 = box.x1 + g_dirty.x1,
                   .y1 = box.y1 + g_dirty.y1,
                   .x2 = box.x1 + g_dirty.x2,
                   .y2 = box.y1 + g_dirty.y2};
    lv_obj_invalidate_area(g_canvas, &a);
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
        uint16_t *dst = g_px + ((y + gy) * g_w) + x;
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
    uint16_t *dst = g_px + (y * g_w);
    for (int32_t i = 0; i < FONT_GLYPH_H * g_w; i++)
        dst[i] = color;
    Dirty(0, y, g_w - 1, y + FONT_GLYPH_H - 1);
}

static void ScrollUp(void *ctx, uint16_t color)
{
    (void)ctx;
    size_t line = (size_t)g_w * FONT_GLYPH_H;
    memmove(g_px, g_px + line, (size_t)(g_h - FONT_GLYPH_H) * (size_t)g_w * sizeof(uint16_t));
    uint16_t *last = g_px + ((size_t)(g_h - FONT_GLYPH_H) * (size_t)g_w);
    for (size_t i = 0; i < line; i++)
        last[i] = color;
    Dirty(0, 0, g_w - 1, g_h - 1);
}

static const VTermOps g_ops = {.draw_cell = DrawCell, .clear_row = ClearRow, .scroll_up = ScrollUp};

static void Print(const char *s)
{
    VTermShowCursor(&g_vt, false);
    while (*s)
        VTermFeed(&g_vt, (uint8_t)*s++);
    VTermShowCursor(&g_vt, true);
    FlushDirty();
}

static void OnTtyKick(void)
{
    if (!g_tty_up)
        return;
    uint8_t buf[128];
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
    FlushDirty();
    if (any)
        Signal(g_tty.doorbell, 1U << g_tty.bit, false);
}

static void TermKey(uint8_t usage, bool pressed)
{
    uint8_t seq[KEYMAP_MAX_SEQ];
    size_t n = KeymapFeed(&g_keymap, usage, pressed, seq, sizeof(seq));
    if (n == 0 || !g_tty_up)
        return;
    ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, seq, (uint32_t)n);
    Signal(g_tty.doorbell, 1U << g_tty.bit, false);
}

static void SetRunning(bool running)
{
    g_running = running;
    GuiSetKeyHook(running && g_canvas ? TermKey : NULL);
    if (g_run)
        lv_obj_set_state(g_run, LV_STATE_DISABLED, running);
    if (g_cmd)
        lv_obj_set_state(g_cmd, LV_STATE_DISABLED, running);
}

static void OnChildExit(void)
{
    if (g_task >= 0)
        HandleClose(g_task);
    g_task = -1;
    if (!g_running)
        return;
    Print("\r\n\x1b[0m\x1b[33m[process exited]\x1b[0m\r\n");
    SetRunning(false);
}

static Err TtySetup(void)
{
    if (g_tty_up)
        return ZUZU_OK;
    if (g_tty_port < 0) {
        g_tty_port = LookupService("/svc/tty");
        if (g_tty_port < 0)
            return ERR_NOENT;
    }
    Err rc = TtyClientConnectDir(g_tty_port, TTY_PROVIDE, TTY_ALIAS, TTY_DIR_BOTH | TTY_EP_EXPLICIT,
                                 GuiEvent(), GUI_BIT_TTY, &g_tty);
    if (rc != ZUZU_OK)
        return rc;
    g_tty_up = true;
    return TtyClientSetWinsz(g_tty_port, &g_tty, g_vt.cols, g_vt.rows);
}

static uint32_t SplitArgs(char *line, char *argbuf, size_t cap, size_t *len, const char **argv0)
{
    uint32_t argc = 0;
    size_t pos = 0;
    char *save = line;
    while (argc < MAX_ARGS) {
        while (*save == ' ')
            save++;
        if (!*save)
            break;
        char *start = save;
        while (*save && *save != ' ')
            save++;
        size_t n = (size_t)(save - start);
        if (pos + n + 1 > cap)
            break;
        memcpy(argbuf + pos, start, n);
        argbuf[pos + n] = '\0';
        if (argc == 0)
            *argv0 = argbuf + pos;
        pos += n + 1;
        argc++;
    }
    *len = pos;
    return argc;
}

static void Run(const char *cmdline)
{
    if (g_running)
        return;
    Err rc = TtySetup();
    if (rc != ZUZU_OK) {
        Print("[cannot provide the gui terminal]\r\n");
        return;
    }
    if (!g_fsd_up) {
        if (FsdConnect(&g_fsd, FSD_SHM_DEFAULT) != ZUZU_OK) {
            Print("[file service unavailable]\r\n");
            return;
        }
        g_fsd_up = true;
    }

    char line[CMD_MAX];
    strncpy(line, cmdline, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    char argbuf[CMD_MAX];
    size_t arglen = 0;
    const char *argv0 = NULL;
    uint32_t argc = SplitArgs(line, argbuf, sizeof(argbuf), &arglen, &argv0);
    if (argc == 0)
        return;

    char path[64];
    if (strchr(argv0, '/'))
        strncpy(path, argv0, sizeof(path) - 1);
    else
        lv_snprintf(path, sizeof(path), "/bin/%s", argv0);
    path[sizeof(path) - 1] = '\0';

    Handle image = -1;
    size_t size = 0;
    if (FsdReadImage(&g_fsd, path, &image, &size) != ZUZU_OK) {
        Print("[not found: ");
        Print(path);
        Print("]\r\n");
        return;
    }

    static const char env[] = "TTY=" TTY_ALIAS "\0PWD=/\0HOME=/";
    SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT,
                      .max_prio = SPAWN_PRIO_INHERIT,
                      .envbuf = env,
                      .envbuf_len = sizeof(env),
                      .envc = 3};
    const char *name = strrchr(path, '/') + 1;
    Handle task = -1;
    rc = SpawnProcessObjAttr(image, size, name, argbuf, arglen, argc, &attr, NULL, &task);
    HandleClose(image);
    if (rc != ZUZU_OK) {
        Print("[cannot run: ");
        Print(StrToError(rc));
        Print("]\r\n");
        return;
    }
    g_task = task;
    Bind(EVENT_TASK, GuiEvent(), task, GUI_BIT_CHILD);
    KeymapInit(&g_keymap);
    SetRunning(true);
}

static void OnRun(lv_event_t *e)
{
    (void)e;
    Run(lv_textarea_get_text(g_cmd));
}

static void InitTerminal(int32_t width, int32_t height)
{
    uint32_t cols = (uint32_t)width / FONT_GLYPH_W;
    uint32_t rows = (uint32_t)height / FONT_GLYPH_H;
    if (cols > VTERM_MAX_COLS)
        cols = VTERM_MAX_COLS;
    if (rows > VTERM_MAX_ROWS)
        rows = VTERM_MAX_ROWS;
    g_w = (int32_t)cols * FONT_GLYPH_W;
    g_h = (int32_t)rows * FONT_GLYPH_H;
    g_px = malloc((size_t)g_w * (size_t)g_h * sizeof(uint16_t));
    if (!g_px)
        return;
    VTermInit(&g_vt, &g_ops, NULL, cols, rows, TERM_FG, TERM_BG);
    GuiSetWakeHook(GUI_BIT_TTY, OnTtyKick);
    GuiSetWakeHook(GUI_BIT_CHILD, OnChildExit);
    g_ready = true;
    Print("Type a command and press Run. Programs built against newlib (kilo, ls, cat, grep)\r\n"
          "draw here; Home leaves them running.\r\n\r\n");
}

static void Open(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 8, 0);

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), TOOLBAR_H);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 8, 0);

    g_cmd = lv_textarea_create(bar);
    lv_textarea_set_one_line(g_cmd, true);
    lv_textarea_set_max_length(g_cmd, CMD_MAX - 1);
    lv_textarea_set_text(g_cmd, DEFAULT_CMD);
    lv_obj_set_flex_grow(g_cmd, 1);
    lv_obj_add_event_cb(g_cmd, OnRun, LV_EVENT_READY, NULL);

    g_run = lv_button_create(bar);
    lv_obj_set_height(g_run, LV_PCT(100));
    lv_obj_add_event_cb(g_run, OnRun, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(g_run);
    lv_label_set_text(label, LV_SYMBOL_PLAY " Run");
    lv_obj_center(label);

    lv_obj_t *area = lv_obj_create(parent);
    lv_obj_remove_style_all(area);
    lv_obj_set_width(area, LV_PCT(100));
    lv_obj_set_flex_grow(area, 1);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(area, lv_color_black(), 0);
    lv_obj_update_layout(parent);

    if (!g_ready)
        InitTerminal(lv_obj_get_content_width(area), lv_obj_get_content_height(area));
    if (!g_ready) {
        lv_obj_t *err = lv_label_create(area);
        lv_label_set_text(err, "Out of memory for the terminal");
        return;
    }

    g_canvas = lv_canvas_create(area);
    lv_canvas_set_buffer(g_canvas, g_px, g_w, g_h, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(g_canvas);
    SetRunning(g_running);
}

static void Close(void)
{
    GuiSetKeyHook(NULL);
    g_canvas = NULL;
    g_cmd = NULL;
    g_run = NULL;
}

const GuiApp g_app_term = {
    .name = "Terminal",
    .symbol = LV_SYMBOL_KEYBOARD,
    .color = 0xbc8cff,
    .open = Open,
    .close = Close,
};
