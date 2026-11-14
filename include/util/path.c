/**
 * @file path.c
 *
 * @brief Absolute path handling shared by the shell and the C library.
 */

#include <stdio.h>
#include <string.h>
#include <util/path.h>

/** @brief Longest single path component. */
#define PATH_PART_MAX 128

bool PathNormalize(const char *path, char *out, size_t out_size)
{
    if (!path || !path[0])
        path = "/";
    if (out_size < 2)
        return false;

    size_t pos = 0;
    out[pos++] = '/';
    out[pos] = '\0';

    const char *p = path;
    while (*p == '/')
        p++;

    while (*p) {
        char part[PATH_PART_MAX];
        size_t len = 0;

        while (*p && *p != '/') {
            if (len + 1 >= sizeof(part))
                return false;
            part[len++] = *p++;
        }
        part[len] = '\0';
        while (*p == '/')
            p++;

        if (len == 0 || (len == 1 && part[0] == '.'))
            continue;

        if (len == 2 && part[0] == '.' && part[1] == '.') {
            if (pos > 1) {
                pos--;
                while (pos > 1 && out[pos - 1] != '/')
                    pos--;
                out[pos] = '\0';
            }
            continue;
        }

        if (pos > 1) {
            if (pos + 1 >= out_size)
                return false;
            out[pos++] = '/';
            out[pos] = '\0';
        }
        if (pos + len >= out_size)
            return false;
        memcpy(out + pos, part, len);
        pos += len;
        out[pos] = '\0';
    }
    return true;
}

bool PathResolve(const char *cwd, const char *path, char *out, size_t out_size)
{
    char raw[PATH_MAX_LEN * 2];

    if (!cwd || !cwd[0])
        cwd = "/";
    if (!path || !path[0])
        path = ".";

    int n;
    if (path[0] == '/')
        n = snprintf(raw, sizeof(raw), "%s", path);
    else
        n = snprintf(raw, sizeof(raw), "%s/%s", cwd, path);
    if (n < 0 || (size_t)n >= sizeof(raw))
        return false;
    return PathNormalize(raw, out, out_size);
}
