#include <dev/tty.h>
#include <util/stdio_core.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/* Raw-mode readers (full-screen editors) do their own echo and line editing. */
int zuzu_console_set_raw(int enable) { return StdioSetRaw(enable); }

/* Lets ttysvc hand the foreground to the next session, and tells the peers of any redirected
 * pipes this process is going away. */
void ConsoleClose(void) { StdioFini(); }

void zuzu_console_on_interrupt(void (*handler)(void)) { StdioOnInterrupt(handler); }

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
    if (!StdioIsTty(fd)) {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}

int tcgetattr(int fd, struct termios *t)
{
    if (!ConsoleIsTty(fd))
        return -1;

    uint32_t m = StdioMode();
    if (TtyClientGetMode(StdioTtyPort(), StdioTtyConn(), &m) != ZUZU_OK)
        m = StdioMode();
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
    if (StdioSetMode(ConsoleTtyFlags(t)) != 0) {
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
    if (TtyClientGetWinsz(StdioTtyPort(), StdioTtyConn(), &cols, &rows) != ZUZU_OK || !arg) {
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
