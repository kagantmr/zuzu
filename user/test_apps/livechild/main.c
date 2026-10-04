/* Attaches to ttysvc and fsd, then dies without closing either session.
 * argv[1]: "fault" (null write) or "quit" (Quit() straight past every
 * destructor). "selfcall-fsd" / "selfcall-tty" Call the client's own liveness
 * port toward that server and block there for good. Anything else returns
 * normally. */

#include <dev/protocols/tty.h>
#include <fs/fsd_client.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define CHILD_TTY_FAILED 2
#define CHILD_FSD_FAILED 3
#define CHILD_OPEN_FAILED 4
#define CHILD_QUIT_STATUS 10

static FsdConn g_fsd;

int main(int argc, char **argv)
{
    if (stdio_open_tty() != 0)
        return CHILD_TTY_FAILED;
    printf("livechild\n");

    if (FsdConnect(&g_fsd, FSD_SHM_DEFAULT) != ZUZU_OK)
        return CHILD_FSD_FAILED;
    uint32_t fd;
    if (FsdOpen(&g_fsd, "/README.md", FSD_MODE_READ, &fd) != ZUZU_OK)
        return CHILD_OPEN_FAILED;

    if (argc > 1 && strcmp(argv[1], "selfcall-fsd") == 0)
        Call(g_fsd.live, 0, -1);
    if (argc > 1 && strcmp(argv[1], "selfcall-tty") == 0) {
        Handle port = LookupService("/svc/tty");
        Handle ev = CreateEvent();
        TtyConn t;
        if (port >= 0 && ev >= 0 && TtyClientConnect(port, TTY_ATTACH, "", ev, 0, &t) == ZUZU_OK)
            Call(t.live, 0, -1);
        return CHILD_TTY_FAILED;
    }
    if (argc > 1 && strcmp(argv[1], "fault") == 0)
        *(volatile int *)0 = 1;
    if (argc > 1 && strcmp(argv[1], "quit") == 0)
        Quit(CHILD_QUIT_STATUS);
    return 0;
}
