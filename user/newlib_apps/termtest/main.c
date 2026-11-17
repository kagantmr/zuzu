#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void Info(void)
{
    struct termios t;
    struct winsize ws;

    if (tcgetattr(0, &t) != 0) {
        printf("tcgetattr failed\n");
        return;
    }
    printf("iflag %04x  oflag %04x  lflag %04x\n", (unsigned)t.c_iflag, (unsigned)t.c_oflag,
           (unsigned)t.c_lflag);
    printf("ICANON %d  ECHO %d  ISIG %d  ICRNL %d  OPOST %d  ONLCR %d\n", !!(t.c_lflag & ICANON),
           !!(t.c_lflag & ECHO), !!(t.c_lflag & ISIG), !!(t.c_iflag & ICRNL), !!(t.c_oflag & OPOST),
           !!(t.c_oflag & ONLCR));
    if (ioctl(0, TIOCGWINSZ, &ws) == 0)
        printf("window %ux%u\n", (unsigned)ws.ws_col, (unsigned)ws.ws_row);
    else
        printf("window size unknown\n");
}

static void Keys(void)
{
    struct termios saved, raw;
    tcgetattr(0, &saved);
    raw = saved;
    cfmakeraw(&raw);
    tcsetattr(0, TCSANOW, &raw);

    printf("raw mode: bytes are shown in hex, q quits\r\n");
    for (;;) {
        unsigned char c;
        if (read(0, &c, 1) != 1)
            break;
        printf("%02x\r\n", c);
        if (c == 'q')
            break;
    }
    tcsetattr(0, TCSANOW, &saved);
}

static void Interrupt(void)
{
    char buf[64];
    printf("cooked read: type a line, or press ^C to be interrupted\n");
    int n = (int)read(0, buf, sizeof(buf) - 1);
    printf("read returned %d (^C should have ended this program before now)\n", n);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "keys") == 0)
        Keys();
    else if (argc > 1 && strcmp(argv[1], "int") == 0)
        Interrupt();
    else {
        Info();
        printf("usage: termtest [keys|int]\n");
    }
    return 0;
}
