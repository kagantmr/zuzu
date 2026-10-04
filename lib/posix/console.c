#include <dev/protocols/tty.h>
#include <errno.h>
#include <string.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define CONSOLE_BIT_KICK 0
#define CONSOLE_POLL_MS 20
#define CONSOLE_ATTACH_RETRIES 100

static Handle console_port;
static Handle console_event = -1;
static TtyConn console_conn;
static int console_ready;
static uint32_t console_mode = TTY_MODE_COOKED | TTY_MODE_ECHO;

static int ConsoleOpen(void)
{
    if (console_ready)
        return 0;

    console_port = LookupService("/svc/tty");
    if (console_port < 0)
        return -1;
    if (console_event < 0) {
        console_event = CreateEvent();
        if (console_event < 0)
            return -1;
    }

    Err rc = ERR_NOENT;
    for (int i = 0; i < CONSOLE_ATTACH_RETRIES && rc == ERR_NOENT; i++) {
        rc = TtyClientConnect(console_port, TTY_ATTACH, "", console_event, CONSOLE_BIT_KICK,
                              &console_conn);
        if (rc == ERR_NOENT)
            Sleep(10);
    }
    if (rc != ZUZU_OK)
        return -1;

    if (TtyClientSetMode(console_port, &console_conn, console_mode) != ZUZU_OK) {
        TtyClientClose(console_port, &console_conn);
        return -1;
    }
    console_ready = 1;
    return 0;
}

/* Raw-mode readers (full-screen editors) do their own echo and line
 * handling; cooked is the default. */
int zuzu_console_set_raw(int enable)
{
    console_mode = enable ? TTY_MODE_RAW : (TTY_MODE_COOKED | TTY_MODE_ECHO);
    if (!console_ready)
        return 0;
    return TtyClientSetMode(console_port, &console_conn, console_mode) == ZUZU_OK ? 0 : -1;
}

/* Lets ttysvc hand the foreground to the next session. Crashed or killed
 * programs never get here; ttysvc has no peer-death detection yet. */
void ConsoleClose(void)
{
    if (!console_ready)
        return;

    TtyShm *shm = console_conn.shm;
    for (int i = 0; i < CONSOLE_ATTACH_RETRIES && ShmRingAvail(&shm->up_hdr) > 0; i++) {
        Signal(console_conn.doorbell, 1U << console_conn.bit, false);
        WaitOn(console_event, CONSOLE_POLL_MS);
    }
    TtyClientClose(console_port, &console_conn);
    console_ready = 0;
}

int ConsoleWrite(const char *buf, int len)
{
    if (ConsoleOpen() != 0) {
        errno = EIO;
        return -1;
    }

    TtyShm *shm = console_conn.shm;
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t left = (uint32_t)len;
    while (left > 0) {
        uint32_t w = ShmRingPush(&shm->up_hdr, shm->up_data, p, left);
        p += w;
        left -= w;
        Signal(console_conn.doorbell, 1U << console_conn.bit, false);
        if (left > 0)
            WaitOn(console_event, CONSOLE_POLL_MS);
    }
    return len;
}

/* Blocks until at least one byte is available; 0 means EOF (^D). */
int ConsoleRead(char *buf, int len)
{
    if (ConsoleOpen() != 0) {
        errno = EIO;
        return -1;
    }

    TtyShm *shm = console_conn.shm;
    for (;;) {
        uint32_t got = ShmRingPop(&shm->down_hdr, shm->down_data, (uint8_t *)buf, (uint32_t)len);
        if (got > 0)
            return (int)got;
        if (TtyEofPending(shm))
            return 0;
        WaitOn(console_event, CONSOLE_POLL_MS);
    }
}
