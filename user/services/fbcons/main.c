#include "fbcons.h"
#include <dev/protocols/fb.h>
#include <dev/keymap.h>
#include <dev/tty.h>
#include <util/vterm.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "fbcons"
#include <util/log.h>

#define BIT_KICK 2
#define FB_EVT_SHIFT 8
#define FB_BITS (FB_EVT_ALL << FB_EVT_SHIFT)
#define MASK(bit) (1U << (bit))
#define ALIAS "fbcons"

static Handle g_event = -1;
static TtyConn g_tty;
static Handle g_tty_port = -1;
static KeymapState g_keymap;
static VTerm g_term;

static void DrawCell(void *ctx, uint32_t col, uint32_t row, char ch, uint16_t fg, uint16_t bg)
{
    (void)ctx;
    DisplayDrawCell(col, row, ch, fg, bg);
}

static void ClearRow(void *ctx, uint32_t row, uint16_t color)
{
    (void)ctx;
    DisplayClearRow(row, color);
}

static void ScrollUp(void *ctx, uint16_t color)
{
    (void)ctx;
    DisplayScrollUp(color);
}

static const VTermOps g_term_ops = {.draw_cell = DrawCell, .clear_row = ClearRow, .scroll_up = ScrollUp};

#define KEY_BATCH 16

static bool Drain(void)
{
    uint8_t buf[64];
    bool any = false;
    ShmRingHdr *hdr = &g_tty.shm->down_hdr;

    VTermShowCursor(&g_term, false);
    for (;;) {
        uint32_t n = ShmRingPop(hdr, g_tty.shm->down_data, buf, sizeof(buf));
        if (n == 0)
            break;
        for (uint32_t i = 0; i < n; i++)
            VTermFeed(&g_term, buf[i]);
        any = true;
    }
    VTermShowCursor(&g_term, true);
    return any;
}

static bool ForwardKeys(void)
{
    InputRing *ring = DisplayInputRing();
    if (!ring)
        return false;
    bool any = false;
    InputEvent ev[KEY_BATCH];
    uint32_t n;
    while ((n = InputRingRead(ring, ev, KEY_BATCH)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            if (ev[i].type != INPUT_EV_KEY)
                continue;
            uint8_t seq[KEYMAP_MAX_SEQ];
            size_t len = KeymapFeed(&g_keymap, (uint8_t)ev[i].code, ev[i].value != 0, seq,
                                    sizeof(seq));
            if (len == 0)
                continue;
            if (ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, seq, (uint32_t)len) != len)
                LOG_WARN(LOG_TAG, "tty input ring full, key dropped");
            any = true;
        }
    }
    return any;
}

static Err SetupTty(void)
{
    while (g_tty_port < 0) {
        g_tty_port = LookupService("/svc/tty");
        if (g_tty_port < 0)
            Sleep(10);
    }
    return TtyClientConnectDir(g_tty_port, TTY_PROVIDE, ALIAS, TTY_DIR_BOTH, g_event, BIT_KICK,
                               &g_tty);
}

int main(void)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return 1;

    Err rc = DisplayInit(g_event, FB_EVT_SHIFT);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "display unavailable rc=%d", rc);
        return 1;
    }

    KeymapInit(&g_keymap);
    VTermInit(&g_term, &g_term_ops, NULL, DisplayCols(), DisplayRows(), DEFAULT_FG, DEFAULT_BG);
    VTermShowCursor(&g_term, true);

    rc = SetupTty();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "tty provide rc=%d", rc);
        return 1;
    }

    rc = TtyClientSetWinsz(g_tty_port, &g_tty, DisplayCols(), DisplayRows());
    if (rc != ZUZU_OK)
        LOG_ERROR(LOG_TAG, "window size rc=%d", rc);

    Handle ready = CreatePort();
    if (ready >= 0)
        RegisterService("/svc/fbcons", ready);

    for (;;) {
        EventWaitResult w = WaitOnEvent(g_event, TIMEOUT_INFINITE, 0, false);
        if (w.status != ZUZU_OK)
            continue;
        uint32_t fb = (w.bits & FB_BITS) >> FB_EVT_SHIFT;
        if (fb & FB_EVT_DEACTIVATE) {
            DisplaySetActive(false);
            KeymapInit(&g_keymap);
            DisplayAckDeactivate();
        }
        if (fb & FB_EVT_ACTIVATE) {
            DisplaySetActive(true);
            DisplayClear(DEFAULT_BG);
            VTermRepaint(&g_term, true);
        }
        bool kick = (fb & FB_EVT_INPUT) && ForwardKeys();
        if (Drain())
            kick = true;
        if (kick)
            Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    }
}
