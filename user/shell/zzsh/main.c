#include "zzsh.h"
#include <ansi.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <fs/fsd_client.h>
#include <stdlib.h>
#include <util/spawn.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>


#define PROMPT ANSI_BOLD ANSI_CYAN "zzsh"  ANSI_GREEN "~>" ANSI_RESET

static FsdConn fsd_conn;
static char cwd[256] = "/";

static void strip(char *s)
{
    char *src = s, *dst = s;
    while (*src == ' ') src++;
    while (*src) {
        if (*src == ' ' && (dst == s || *(dst - 1) == ' '))
            src++;
        else
            *dst++ = *src++;
    }
    if (dst > s && *(dst - 1) == ' ') dst--;
    *dst = '\0';
}

static bool ensure_fsd(void)
{
    if (fsd_conn.ready)
        return true;
    return FsdConnect(&fsd_conn, FSD_SHM_DEFAULT) == ZUZU_OK;
}

static bool normalize_path(const char *path, char *out, size_t out_size)
{
    if (!path || !path[0])
        path = "/";

    size_t pos = 0;
    out[pos++] = '/';
    out[pos] = '\0';

    const char *p = path;
    while (*p == '/')
        p++;

    while (*p) {
        char segment[128];
        size_t segment_len = 0;

        while (*p && *p != '/') {
            if (segment_len + 1 >= sizeof(segment))
                return false;
            segment[segment_len++] = *p++;
        }
        segment[segment_len] = '\0';

        while (*p == '/')
            p++;

        if (segment_len == 0 || (segment_len == 1 && segment[0] == '.'))
            continue;

        if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
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

        if (pos + segment_len >= out_size)
            return false;
        memcpy(out + pos, segment, segment_len);
        pos += segment_len;
        out[pos] = '\0';
    }

    return true;
}

static bool resolve_path(const char *input, char *out, size_t out_size)
{
    char raw[512];

    if (!input || !input[0]) {
        if (strlen(cwd) + 1 > sizeof(raw) || strlen(cwd) + 1 > out_size)
            return false;
        memcpy(out, cwd, strlen(cwd) + 1);
        return true;
    }

    if (input[0] == '/') {
        if (strlen(input) + 1 > sizeof(raw))
            return false;
        memcpy(raw, input, strlen(input) + 1);
    } else if (strcmp(cwd, "/") == 0) {
        if (snprintf(raw, sizeof(raw), "/%s", input) >= (int)sizeof(raw))
            return false;
    } else {
        if (snprintf(raw, sizeof(raw), "%s/%s", cwd, input) >= (int)sizeof(raw))
            return false;
    }

    return normalize_path(raw, out, out_size);
}

static bool stat_path(const char *path, FsdStat *st)
{
    if (!ensure_fsd())
        return false;

    if (strlen(path) >= 4096)
        return false;

    return FsdGetStat(&fsd_conn, path, st) == ZUZU_OK;
}

/* ---- ls ---- */

static void cmd_ls(const char *arg)
{
    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "ls: fsd unavailable\n" ANSI_RESET);
        return;
    }

    char path[256];
    if (!resolve_path(arg, path, sizeof(path))) {
        printf("%s", "path too long\n");
        return;
    }

    char line[96];
    uint32_t start = 0;

    /* fsd returns at most a bufferful of dirents per call; page through the
     * directory by advancing `start` until a short batch ends it. */
    for (;;) {
        FsdDirEntry entries[32];
        uint32_t count = 0;
        if (FsdReadDir(&fsd_conn, path, start, entries,
                        sizeof(entries) / sizeof(entries[0]), &count) != ZUZU_OK) {
            if (start == 0)
                printf("%s", ANSI_RED "ls: cannot read directory\n" ANSI_RESET);
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].type == FSD_TYPE_DIR) {
                (void)snprintf(line, sizeof(line), ANSI_BOLD ANSI_CYAN "%-13s" ANSI_RESET "  <DIR>\n",
                         entries[i].name);
            } else {
                (void)snprintf(line, sizeof(line), "%-13s  %u\n",
                         entries[i].name, entries[i].size);
            }
            printf("%s", line);
        }

        if (count < sizeof(entries) / sizeof(entries[0]))
            break;
        start += count;
    }
}

/* ---- cat ---- */

static void cmd_cat(const char *path)
{
    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "cat: fsd unavailable\n" ANSI_RESET);
        return;
    }

    if (!path || !path[0]) {
        printf("%s", "usage: cat <file>\n");
        return;
    }

    char abs_path[256];
    if (!resolve_path(path, abs_path, sizeof(abs_path))) {
        printf("%s", "cat: path too long\n");
        return;
    }

    uint32_t fd = 0;
    if (FsdOpen(&fsd_conn, abs_path, FSD_MODE_READ, &fd) != ZUZU_OK) {
        printf("%s", ANSI_RED "cat: file not found\n" ANSI_RESET);
        return;
    }

    /* printf formats into a 1 KiB buffer, so keep chunks well under that. */
    char chunk[512];
    while (1) {
        uint32_t got = 0;
        if (FsdRead(&fsd_conn, fd, chunk, sizeof(chunk) - 1, &got) != ZUZU_OK)
            break;
        if (got == 0) break;

        chunk[got] = '\0';  /* null-terminate for printf */
        printf("%s", chunk);
    }

    FsdClose(&fsd_conn, fd);
    printf("\n");
}

/* ---- run ---- */

static void *read_file(const char *path, size_t *len)
{
    FsdStat st;
    uint32_t fd;
    if (FsdGetStat(&fsd_conn, path, &st) != ZUZU_OK || st.type != FSD_TYPE_FILE)
        return NULL;
    if (FsdOpen(&fsd_conn, path, FSD_MODE_READ, &fd) != ZUZU_OK)
        return NULL;

    uint8_t *buf = malloc(st.size ? st.size : 1);
    size_t off = 0;
    while (buf && off < st.size) {
        uint32_t got = 0;
        if (FsdRead(&fsd_conn, fd, buf + off, st.size - (uint32_t)off, &got) != ZUZU_OK || got == 0)
            break;
        off += got;
    }
    FsdClose(&fsd_conn, fd);

    if (off != st.size) {
        free(buf);
        return NULL;
    }
    *len = off;
    return buf;
}

static void cmd_run(const char *line)
{
    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "zzsh: fsd unavailable\n" ANSI_RESET);
        return;
    }

    char buf[LINE_BUFFER_SIZE];
    memcpy(buf, line, strlen(line) + 1);

    char argbuf[LINE_BUFFER_SIZE];
    size_t argpos = 0;
    uint32_t argc = 0;
    char *p = buf;
    char *cmd = NULL;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        char *tok = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
        if (!cmd)
            cmd = tok;
        size_t n = strlen(tok) + 1;
        memcpy(argbuf + argpos, tok, n);
        argpos += n;
        argc++;
    }
    if (!cmd)
        return;

    char path[256];
    size_t len = 0;
    void *image = NULL;
    if (strchr(cmd, '/')) {
        if (resolve_path(cmd, path, sizeof(path)))
            image = read_file(path, &len);
    } else {
        char rel[256];
        if (snprintf(rel, sizeof(rel), "/bin/%s", cmd) < (int)sizeof(rel) &&
            resolve_path(rel, path, sizeof(path)))
            image = read_file(path, &len);
        if (!image && resolve_path(cmd, path, sizeof(path)))
            image = read_file(path, &len);
    }
    if (!image) {
        printf("zzsh: %s: command not found\n", cmd);
        return;
    }

    /* Release our tty session before the child attaches, so it becomes the
     * foreground; stdio re-attaches on the next printf/getchar. */
    stdio_close_tty();

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    Spid pid;
    Handle task;
    Err rc = SpawnProcess(image, len, name, argbuf, argpos, argc, &pid, &task);
    free(image);
    if (rc != ZUZU_OK) {
        printf(ANSI_RED "zzsh: %s: spawn failed (err %d)\n" ANSI_RESET, cmd, rc);
        return;
    }

    WaitOn(task, TIMEOUT_INFINITE);
    HandleClose(task);
    HandleClose((Handle)pid);
}

/* ---- dispatch ---- */

static void cmd_help(void)
{
    printf("%s",
        ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET
        ANSI_BOLD "  help" ANSI_RESET "          show this message\n"
        ANSI_BOLD "  clear" ANSI_RESET "         clear the screen\n"
        ANSI_BOLD "  pwd" ANSI_RESET "           print current directory\n"
        ANSI_BOLD "  cd <path>" ANSI_RESET "     change current directory\n"
        ANSI_BOLD "  ls [path]" ANSI_RESET "     list directory\n"
        ANSI_BOLD "  cat <file>" ANSI_RESET "    print file contents\n"
        ANSI_BOLD "  exit" ANSI_RESET "          leave the shell\n"
        ANSI_BOLD "  <program>" ANSI_RESET "     run /bin/<program> or a path\n");
}

static void cmd_cd(const char *arg)
{
    char path[256];
    FsdStat st;

    if (!arg || !arg[0]) {
        printf("%s", "usage: cd <path>\n");
        return;
    }

    if (!resolve_path(arg, path, sizeof(path))) {
        printf("%s", "cd: path too long\n");
        return;
    }

    if (strcmp(path, "/") != 0) {
        if (!stat_path(path, &st)) {
            printf("%s", ANSI_RED "cd: not found\n" ANSI_RESET);
            return;
        }
        if (st.type != FSD_TYPE_DIR) {
            printf("%s", ANSI_RED "cd: not a directory\n" ANSI_RESET);
            return;
        }
    }

    memcpy(cwd, path, strlen(path) + 1);
}

/* Returns false when the shell should exit. */
bool command_dispatch(const char *line)
{
    if (strcmp(line, "exit") == 0)
        return false;

    if (strcmp(line, "help") == 0)
        cmd_help();
    else if (strcmp(line, "clear") == 0)
        printf("%s", ANSI_CLEAR);
    else if (strcmp(line, "pwd") == 0)
        printf("%s\n", cwd);
    else if (strcmp(line, "cd") == 0)
        cmd_cd(NULL);
    else if (strncmp(line, "cd ", 3) == 0)
        cmd_cd(line + 3);
    else if (strcmp(line, "ls") == 0)
        cmd_ls(NULL);
    else if (strncmp(line, "ls ", 3) == 0)
        cmd_ls(line + 3);
    else if (strcmp(line, "cat") == 0)
        cmd_cat(NULL);
    else if (strncmp(line, "cat ", 4) == 0)
        cmd_cat(line + 4);
    else
        cmd_run(line);

    return true;
}

static char history[HISTORY_MAX][LINE_BUFFER_SIZE];
static int hist_head;  /* next write slot */
static int hist_count;

static void hist_push(const char *line)
{
    int last = (hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (hist_count > 0 && strcmp(history[last], line) == 0)
        return;
    memcpy(history[hist_head], line, strlen(line) + 1);
    hist_head = (hist_head + 1) % HISTORY_MAX;
    if (hist_count < HISTORY_MAX)
        hist_count++;
}

/* offset 1 = most recent */
static const char *hist_get(int offset)
{
    if (offset < 1 || offset > hist_count)
        return NULL;
    return history[(hist_head - offset + HISTORY_MAX) % HISTORY_MAX];
}

static void redraw_line(const char *line)
{
    printf("\r%s%s\033[K", PROMPT, line);
}

/* Raw mode: zzsh does its own echo, backspace and history. Returns false on EOF (^D on an empty line). */
static bool read_line(char *line)
{
    enum { ST_NORMAL, ST_ESC, ST_CSI } state = ST_NORMAL;
    char saved[LINE_BUFFER_SIZE];
    size_t pos = 0;
    int hist_pos = 0;

    line[0] = '\0';
    for (;;)
    {
        int c = getchar();
        if (c == EOF)
            return false;

        if (state == ST_ESC) {
            state = (c == '[') ? ST_CSI : ST_NORMAL;
            continue;
        }
        if (state == ST_CSI) {
            state = ST_NORMAL;
            const char *h = NULL;
            if (c == 'A' && hist_pos < hist_count) {
                if (hist_pos == 0)
                    memcpy(saved, line, pos + 1);
                h = hist_get(++hist_pos);
            } else if (c == 'B' && hist_pos > 0) {
                h = --hist_pos ? hist_get(hist_pos) : saved;
            }
            if (h) {
                pos = strlen(h);
                memcpy(line, h, pos + 1);
                redraw_line(line);
            }
            continue;
        }

        if (c == '\033') {
            state = ST_ESC;
        } else if (c == '\r' || c == '\n') {
            printf("\n");
            return true;
        } else if (c == 0x04) {
            if (pos == 0)
                return false;
        } else if (c == 0x03) {
            printf("^C\n");
            line[0] = '\0';
            return true;
        } else if (c == 0x15) {
            pos = 0;
            line[0] = '\0';
            redraw_line(line);
        } else if (c == 127 || c == '\b') {
            if (pos > 0) {
                line[--pos] = '\0';
                printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7f && pos < LINE_BUFFER_SIZE - 1) {
            line[pos++] = (char)c;
            line[pos] = '\0';
            printf("%c", c);
        }
    }
}

static void __attribute__((destructor)) ShellDetachFsd(void)
{
    FsdDetach(&fsd_conn);
}

int main(void)
{
    stdio_set_raw(1);
    if (stdio_open_tty() != 0)
        return 1;

    printf("%s", ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET);

    for (;;)
    {
        printf("%s", PROMPT);

        char line[LINE_BUFFER_SIZE];
        if (!read_line(line))
            break;

        strip(line);
        if (!line[0])
            continue;
        hist_push(line);
        if (!command_dispatch(line))
            break;
    }

    printf("\n");
    return 0;
}
