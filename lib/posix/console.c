#include <dev/tty.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define CONSOLE_DRAIN_MS 20 /* bounds the exit-time flush if no provider ever drains */
#define CONSOLE_BIT_KICK 0
#define CONSOLE_ATTACH_RETRIES 100

static Handle console_port;
static Handle console_event = -1;
static TtyConn console_conn;
static int console_ready;
static int console_focused;
static uint32_t console_mode = TTY_MODE_COOKED | TTY_MODE_ECHO;
static uint32_t console_intr_seen;
static void (*console_intr_handler)(void);

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
    console_intr_seen = console_conn.shm->ctl.intr_seq;
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
 * programs never get here; ttysvc reaps those through their liveness port. */
void ConsoleClose(void)
{
    if (!console_ready)
        return;

    TtyShm *shm = console_conn.shm;
    for (int i = 0; i < CONSOLE_ATTACH_RETRIES && ShmRingAvail(&shm->up_hdr) > 0; i++) {
        Signal(console_conn.doorbell, 1U << console_conn.bit, false);
        WaitOn(console_event, CONSOLE_DRAIN_MS);
    }
    TtyClientClose(console_port, &console_conn);
    console_ready = 0;
    console_focused = 0;
}

void zuzu_console_on_interrupt(void (*handler)(void)) { console_intr_handler = handler; }

static void ConsoleCheckInterrupt(void)
{
    if (!console_ready || !TtyInterrupted(console_conn.shm, &console_intr_seen))
        return;
    if (console_intr_handler) {
        console_intr_handler();
        return;
    }
    ConsoleClose();
    Quit(130);
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
    ConsoleCheckInterrupt();
    while (left > 0) {
        uint32_t w = ShmRingPush(&shm->up_hdr, shm->up_data, p, left);
        p += w;
        left -= w;
        Signal(console_conn.doorbell, 1U << console_conn.bit, false);
        if (left > 0) {
            WaitOn(console_event, TIMEOUT_INFINITE);
            ConsoleCheckInterrupt();
        }
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

    if (!console_focused) {
        if (TtyClientFocus(console_port, &console_conn) != ZUZU_OK) {
            errno = EIO;
            return -1;
        }
        console_focused = 1;
    }

    TtyShm *shm = console_conn.shm;
    for (;;) {
        uint32_t got = ShmRingPop(&shm->down_hdr, shm->down_data, (uint8_t *)buf, (uint32_t)len);
        if (got > 0) {
            Signal(console_conn.doorbell, 1U << console_conn.bit, false);
            return (int)got;
        }
        ConsoleCheckInterrupt();
        if (TtyEofPending(shm))
            return 0;
        WaitOn(console_event, TIMEOUT_INFINITE);
    }
}

static struct termios console_termios;
static int console_termios_ready;

static void ConsoleTermiosInit(void)
{
    if (console_termios_ready)
        return;
    memset(&console_termios, 0, sizeof(console_termios));
    console_termios.c_iflag = IXON;
    console_termios.c_cflag = CS8 | CREAD | CLOCAL | B115200;
    console_termios.c_ispeed = B115200;
    console_termios.c_ospeed = B115200;
    console_termios.c_cc[VINTR] = 0x03;
    console_termios.c_cc[VQUIT] = 0x1C;
    console_termios.c_cc[VERASE] = 0x7F;
    console_termios.c_cc[VKILL] = 0x15;
    console_termios.c_cc[VEOF] = 0x04;
    console_termios.c_cc[VSTART] = 0x11;
    console_termios.c_cc[VSTOP] = 0x13;
    console_termios.c_cc[VSUSP] = 0x1A;
    console_termios.c_cc[VWERASE] = 0x17;
    console_termios.c_cc[VMIN] = 1;
    console_termios_ready = 1;
}

static void ConsoleApplyFlags(struct termios *t, uint32_t m)
{
    t->c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    t->c_iflag &= ~(tcflag_t)ICRNL;
    t->c_oflag &= ~(tcflag_t)(OPOST | ONLCR);
    if (m & TTY_ICANON)
        t->c_lflag |= ICANON;
    if (m & TTY_ECHO)
        t->c_lflag |= ECHO;
    if (m & TTY_ISIG)
        t->c_lflag |= ISIG;
    if (m & TTY_ICRNL)
        t->c_iflag |= ICRNL;
    if (m & TTY_OPOST)
        t->c_oflag |= OPOST;
    if (m & TTY_ONLCR)
        t->c_oflag |= ONLCR;
}

static uint32_t ConsoleTtyFlags(const struct termios *t)
{
    uint32_t m = 0;
    if (t->c_lflag & ICANON)
        m |= TTY_ICANON;
    if (t->c_lflag & ECHO)
        m |= TTY_ECHO;
    if (t->c_lflag & ISIG)
        m |= TTY_ISIG;
    if (t->c_iflag & ICRNL)
        m |= TTY_ICRNL;
    if (t->c_oflag & OPOST)
        m |= TTY_OPOST;
    if ((t->c_oflag & OPOST) && (t->c_oflag & ONLCR))
        m |= TTY_ONLCR;
    return m;
}

static int ConsoleIsTty(int fd)
{
    if (fd < 0 || fd > 2 || ConsoleOpen() != 0) {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}

int tcgetattr(int fd, struct termios *t)
{
    if (!ConsoleIsTty(fd))
        return -1;

    uint32_t m = console_mode;
    if (TtyClientGetMode(console_port, &console_conn, &m) != ZUZU_OK)
        m = console_mode;
    ConsoleTermiosInit();
    ConsoleApplyFlags(&console_termios, m);
    *t = console_termios;
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
    (void)action;
    if (!ConsoleIsTty(fd))
        return -1;

    ConsoleTermiosInit();
    console_termios = *t;
    console_mode = ConsoleTtyFlags(t);
    if (TtyClientSetMode(console_port, &console_conn, console_mode) != ZUZU_OK) {
        errno = EIO;
        return -1;
    }
    return 0;
}

void cfmakeraw(struct termios *t)
{
    t->c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~(tcflag_t)OPOST;
    t->c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

speed_t cfgetispeed(const struct termios *t) { return t->c_ispeed; }

speed_t cfgetospeed(const struct termios *t) { return t->c_ospeed; }

int cfsetispeed(struct termios *t, speed_t speed)
{
    t->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed)
{
    t->c_ospeed = speed;
    return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (request != TIOCGWINSZ || !ConsoleIsTty(fd)) {
        if (request != TIOCGWINSZ)
            errno = ENOTTY;
        return -1;
    }

    uint32_t cols = 0;
    uint32_t rows = 0;
    if (TtyClientGetWinsz(console_port, &console_conn, &cols, &rows) != ZUZU_OK || !arg) {
        errno = ENOTTY;
        return -1;
    }
    struct winsize *ws = arg;
    ws->ws_col = (unsigned short)cols;
    ws->ws_row = (unsigned short)rows;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
    return 0;
}
