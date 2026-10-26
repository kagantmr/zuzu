#include "fbcons.h"
#include <dev/tty.h>
#include <zuzu/service.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "fbcons"
#include <util/log.h>

#define BIT_KICK 2
#define MASK(bit) (1U << (bit))
#define ALIAS "fbcons"

static Handle g_event = -1;
static TtyConn g_tty;
static Handle g_tty_port = -1;

static bool Drain(void)
{
    uint8_t buf[64];
    bool any = false;
    ShmRingHdr *hdr = &g_tty.shm->down_hdr;

    TermShowCursor(false);
    for (;;) {
        uint32_t n = ShmRingPop(hdr, g_tty.shm->down_data, buf, sizeof(buf));
        if (n == 0)
            break;
        for (uint32_t i = 0; i < n; i++)
            TermFeed(buf[i]);
        any = true;
    }
    TermShowCursor(true);
    return any;
}

static Err SetupTty(void)
{
    while (g_tty_port < 0) {
        g_tty_port = LookupService("/svc/tty");
        if (g_tty_port < 0)
            Sleep(10);
    }
    return TtyClientConnectDir(g_tty_port, TTY_PROVIDE, ALIAS, TTY_DIR_OUT, g_event, BIT_KICK,
                               &g_tty);
}

int main(void)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return 1;

    Err rc = DisplayInit();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "display unavailable rc=%d", rc);
        return 1;
    }

    TermInit();
    TermShowCursor(true);

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
        WaitOn(g_event, TIMEOUT_INFINITE);
        if (Drain())
            Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    }
}
