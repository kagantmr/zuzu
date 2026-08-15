/* POSIX basename()/dirname(): declared in this newlib's <libgen.h> but not
 * implemented (no libc.a object for either). Both may modify path in place
 * and/or return a pointer into static storage per POSIX. */
#include <string.h>

static char g_buf[2];

char *basename(char *path)
{
    if (!path || !*path) {
        g_buf[0] = '.';
        g_buf[1] = '\0';
        return g_buf;
    }

    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    if (len == 1 && path[0] == '/')
        return path;

    char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char *dirname(char *path)
{
    if (!path || !*path) {
        g_buf[0] = '.';
        g_buf[1] = '\0';
        return g_buf;
    }

    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    char *slash = strrchr(path, '/');
    if (!slash) {
        g_buf[0] = '.';
        g_buf[1] = '\0';
        return g_buf;
    }

    if (slash == path)
        return "/";

    *slash = '\0';
    return path;
}
