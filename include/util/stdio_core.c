/**
 * @file stdio_core.c
 *
 * @brief Standard input, output and error as pipes.
 */

#include <string.h>
#include <util/stdio_core.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/** @brief Bit ttysvc rings on our event. */
#define STDIO_BIT_KICK 0
/** @brief Longest wait per step when flushing at exit */
#define STDIO_DRAIN_MS 20
/** @brief Attach attempts (10 ms apart) before giving up on the endpoint. */
#define STDIO_ATTACH_RETRIES 100

/** @brief State of the terminal session. */
typedef enum {
    STDIO_IDLE,   /**< Not connected. */
    STDIO_READY,  /**< Connected. */
    STDIO_FAILED, /**< The endpoint never appeared. */
} StdioState;

static StdioState g_state = STDIO_IDLE; /**< Where the terminal session stands. */
static Handle g_port = -1;              /**< Handle to ttysvc. */
static Handle g_event = -1;             /**< Our event, rung by ttysvc. */
static TtyConn g_conn;                  /**< The terminal session. */
static PipeEnd g_tty[3];                /**< The session's rings as stdin, stdout, stderr. */
static char g_alias[TTY_NAME_MAX];      /**< Endpoint to attach to; empty for the default. */
static uint32_t g_mode = TTY_MODE_COOKED | TTY_MODE_ECHO; /**< Mode to set on connecting. */
static bool g_focused;               /**< Whether we have taken the foreground. */
static void (*g_intr_handler)(void); /**< What to run on ^C, or NULL to end the program. */

/** @brief Flushes output, leaves the session and forgets the pipe ends. */
static void Disconnect(void)
{
    if (g_state == STDIO_READY) {
        TtyShm *shm = g_conn.shm;
        for (int i = 0; i < STDIO_ATTACH_RETRIES &&
                        (ShmRingAvail(&shm->up_hdr) > 0 || ShmRingAvail(&shm->err_hdr) > 0);
             i++) {
            Signal(g_conn.doorbell, 1U << g_conn.bit, false);
            WaitOn(g_event, STDIO_DRAIN_MS);
        }
        TtyClientClose(g_port, &g_conn);
        for (int i = 0; i < 3; i++)
            PipeDetach(&g_tty[i]);
    }
    g_state = STDIO_IDLE;
    g_focused = false;
}

int StdioOpenTty(void)
{
    if (g_state == STDIO_READY)
        return 0;
    if (g_state == STDIO_FAILED)
        return -1;

    if (g_port < 0) {
        g_port = LookupService("/svc/tty");
        if (g_port < 0) {
            g_port = -1;
            return -1;
        }
    }
    if (g_event < 0) {
        g_event = CreateEvent();
        if (g_event < 0) {
            g_event = -1;
            return -1;
        }
    }

    Err rc = ERR_NOENT;
    for (int i = 0; i < STDIO_ATTACH_RETRIES && rc == ERR_NOENT; i++) {
        rc = TtyClientConnect(g_port, TTY_ATTACH, g_alias, g_event, STDIO_BIT_KICK, &g_conn);
        if (rc == ERR_NOENT)
            Sleep(10);
    }
    if (rc != ZUZU_OK) {
        if (rc == ERR_NOENT)
            g_state = STDIO_FAILED;
        return -1;
    }

    rc = TtyClientSetMode(g_port, &g_conn, g_mode);
    if (rc != ZUZU_OK) {
        TtyClientClose(g_port, &g_conn);
        return -1;
    }
    TtyConnPipes(&g_conn, &g_tty[0], &g_tty[1], &g_tty[2]);
    g_state = STDIO_READY;
    return 0;
}

void StdioCloseTty(void) { Disconnect(); }

int StdioRouteTty(const char *alias)
{
    if (!alias || strlen(alias) >= TTY_NAME_MAX)
        return -1;

    Disconnect();
    strcpy(g_alias, alias);
    g_state = STDIO_IDLE;
    return StdioOpenTty();
}

PipeEnd *StdioStream(int fd)
{
    PipeEnd *redirected = (fd == 0)  ? StdioRedirIn()
                          : (fd == 1) ? StdioRedirOut()
                          : (fd == 2) ? StdioRedirErr()
                                    : NULL;
    if (redirected)
        return redirected;
    if (fd < 0 || fd > 2 || StdioOpenTty() != 0)
        return NULL;
    return &g_tty[fd];
}

bool StdioIsTty(int fd)
{
    if (fd < 0 || fd > 2)
        return false;
    bool redirected = fd == 0   ? StdioRedirIn() != NULL
                      : fd == 1 ? StdioRedirOut() != NULL
                                : StdioRedirErr() != NULL;
    return !redirected && StdioOpenTty() == 0;
}

void StdioOnInterrupt(void (*handler)(void)) { g_intr_handler = handler; }

/**
 * @brief ^C while ISIG is set: runs the program's handler, or ends the program.
 *
 * @param[in] e The end that was interrupted.
 */
static void Interrupted(PipeEnd *e)
{
    PipeAckInterrupt(e);
    if (g_intr_handler) {
        g_intr_handler();
        return;
    }
    StdioFini();
    Quit(130);
}

int StdioRead(void *buf, size_t cap)
{
    PipeEnd *in = StdioStream(0);
    if (!in)
        return ERR_NOTCONN;

    if (g_state == STDIO_READY && in == &g_tty[0] && !g_focused) {
        if (TtyClientFocus(g_port, &g_conn) != ZUZU_OK)
            return ERR_IO;
        g_focused = true;
    }

    for (;;) {
        int n = PipeRead(in, buf, cap, TIMEOUT_INFINITE);
        if (n != ERR_INTR)
            return n;
        Interrupted(in);
    }
}

int StdioWrite(int fd, const void *buf, size_t len)
{
    PipeEnd *out = (fd == 1 || fd == 2) ? StdioStream(fd) : NULL;
    if (!out)
        return ERR_NOTCONN;

    if (PipeInterrupted(out))
        Interrupted(out);

    const uint8_t *p = buf;
    size_t left = len;
    while (left > 0) {
        int n = PipeWrite(out, p, left, TIMEOUT_INFINITE);
        if (n == ERR_INTR) {
            Interrupted(out);
            continue;
        }
        if (n < 0)
            return n;
        p += n;
        left -= (size_t)n;
    }
    return (int)len;
}

int StdioSetMode(uint32_t flags)
{
    g_mode = flags;
    if (g_state != STDIO_READY)
        return 0;
    return TtyClientSetMode(g_port, &g_conn, g_mode) == ZUZU_OK ? 0 : -1;
}

int StdioSetRaw(int enable)
{
    return StdioSetMode(enable ? TTY_MODE_RAW : (TTY_MODE_COOKED | TTY_MODE_ECHO));
}

uint32_t StdioMode(void) { return g_mode; }

TtyConn *StdioTtyConn(void) { return g_state == STDIO_READY ? &g_conn : NULL; }

Handle StdioTtyPort(void) { return g_state == STDIO_READY ? g_port : -1; }

void StdioFini(void)
{
    Disconnect();
    StdioRedirClose();
}
