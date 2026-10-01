#include <dev/protocols/tty.h>
#include <snprintf.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define BIT_KICK 0
#define POLL_MS 20
#define MASK(bit) (1U << (bit))

static Handle g_event = -1;
static TtyConn g_tty;

static void Kick(void)
{
    Err rc = Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    if (rc != ZUZU_OK)
        UserspaceDebugLog("ttytest: kick failed rc=%d", rc);
}

/* Blocks (bounded by ttysvc draining us) when the up ring is full: that is
 * the backpressure a background consumer sees. */
static void Print(const char *s)
{
    size_t n = strlen(s);
    while (n > 0)
    {
        uint32_t w = ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, (const uint8_t *)s, (uint32_t)n);
        s += w;
        n -= w;
        Kick();
        if (n > 0)
            WaitOn(g_event, POLL_MS);
    }
}

static Handle WaitForTty(void)
{
    for (;;)
    {
        Handle h = LookupService("/svc/tty");
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

int main(void)
{
    g_event = CreateEvent();
    Handle port = WaitForTty();

    /* Consumers may start before any provider has registered an endpoint. */
    Err rc;
    while ((rc = TtyClientConnect(port, TTY_ATTACH, "", g_event, BIT_KICK, &g_tty)) == ERR_NOENT)
        Sleep(10);
    UserspaceDebugLog("ttytest: attach rc=%d index=%u", rc, g_tty.index);
    if (rc != ZUZU_OK)
        return rc;
    rc = TtyClientSetMode(port, &g_tty, TTY_MODE_COOKED | TTY_MODE_ECHO);
    UserspaceDebugLog("ttytest: setmode rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    char banner[96];
    snprintf(banner, sizeof(banner), "ttytest #%u: type a line (^D to quit)\n", g_tty.index);
    Print(banner);

    uint32_t count = 0;
    uint32_t seen_intr = 0;
    char line[128];
    uint32_t len = 0;
    TtyShm *shm = g_tty.shm;

    for (;;)
    {
        WaitOn(g_event, POLL_MS);

        uint8_t b;
        while (ShmRingPop(&shm->down_hdr, shm->down_data, &b, 1) == 1)
        {
            if (b != '\n')
            {
                if (len < sizeof(line) - 1)
                    line[len++] = (char)b;
                continue;
            }
            line[len] = '\0';
            char out[192];
            snprintf(out, sizeof(out), "[%u] you typed: %s\n", ++count, line);
            Print(out);
            len = 0;
        }

        if (TtyInterrupted(shm, &seen_intr))
            Print("(^C seen)\n");

        if (TtyEofPending(shm))
            break;
    }

    Print("bye\n");
    /* Let ttysvc drain our last words before the session goes away. */
    for (int i = 0; i < 100 && ShmRingAvail(&shm->up_hdr) > 0; i++)
    {
        Kick();
        WaitOn(g_event, POLL_MS);
    }
    TtyClientClose(port, &g_tty);
    UserspaceDebugLog("ttytest: exiting");
    return 0;
}
