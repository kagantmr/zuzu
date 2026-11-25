#include <stdio.h>
#include <string.h>
#include <zgfx/win.h>
#include <zuzu/zuzu.h>

#define CONNECT_WAIT_MS 2000
#define TEXT_MAX 64

static int Usage(void)
{
    printf("usage: splash <text...> | splash done\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return Usage();
    WinConn conn;
    Err rc = WinConnect(&conn, CONNECT_WAIT_MS);
    if (rc != ZUZU_OK) {
        printf("splash: no window manager: %s\n", StrToError(rc));
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "done") == 0) {
        rc = WinBootDone(&conn);
    } else {
        char text[TEXT_MAX] = "";
        size_t len = 0;
        for (int i = 1; i < argc; i++) {
            int n = snprintf(text + len, sizeof(text) - len, "%s%s", i > 1 ? " " : "", argv[i]);
            if (n < 0 || (size_t)n >= sizeof(text) - len)
                break;
            len += (size_t)n;
        }
        rc = WinBootStatus(&conn, text);
    }
    if (rc != ZUZU_OK) {
        printf("splash: %s\n", StrToError(rc));
        return 1;
    }
    return 0;
}
