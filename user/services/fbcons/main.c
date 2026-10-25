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

    Handle ready = CreatePort();
    if (ready >= 0)
        RegisterService("/svc/fbcons", ready);

    for (;;) {
        WaitOn(g_event, TIMEOUT_INFINITE);
        if (Drain())
            Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    }
}
