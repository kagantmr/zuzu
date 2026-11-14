#include "zzsh.h"
#include <ansi.h>
#include <fs/fsd_client.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/imgcache.h>
#include <util/pipe.h>
#include <util/spawn.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

#define PROMPT ANSI_BOLD ANSI_CYAN "zzsh " ANSI_GREEN "~>" ANSI_RESET

static FsdConn fsd_conn;
static char cwd[256] = "/";

static void strip(char *s)
{
    char *src = s, *dst = s;
    while (*src == ' ')
        src++;
    while (*src) {
        if (*src == ' ' && (dst == s || *(dst - 1) == ' '))
            src++;
        else
            *dst++ = *src++;
    }
    if (dst > s && *(dst - 1) == ' ')
        dst--;
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

/* ---- run ---- */

static ImgCache g_images;

/* Returns the image object for path, from the cache when the file is unchanged. *owned is
 * true when the caller must close the handle, false when the cache keeps it. */
static Handle load_image(const char *path, size_t *len, bool *owned)
{
    FsdStat st;
    if (FsdGetStat(&fsd_conn, path, &st) != ZUZU_OK || st.type != FSD_TYPE_FILE || st.size == 0)
        return -1;

    *len = st.size;
    Handle obj = ImgCacheGet(&g_images, path, st.size, st.mtime);
    if (obj >= 0) {
        *owned = false;
        return obj;
    }

    if (FsdReadImage(&fsd_conn, path, &obj, len) != ZUZU_OK)
        return -1;
    *owned = !ImgCachePut(&g_images, path, st.size, st.mtime, obj);
    return obj;
}

#define PIPELINE_MAX 8

/* Splits `text` into argbuf/argc, finds the program and starts it with the given stdio pipes.
 * Returns false after printing why. */
static bool spawn_cmd(const char *text, const PipeEnd *in, const PipeEnd *out, Handle *task_out,
                      Spid *pid_out)
{
    char buf[LINE_BUFFER_SIZE];
    memcpy(buf, text, strlen(text) + 1);

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
        return false;

    char path[256];
    size_t len = 0;
    Handle image = -1;
    bool owned = true;
    if (strchr(cmd, '/')) {
        if (resolve_path(cmd, path, sizeof(path)))
            image = load_image(path, &len, &owned);
    } else {
        char rel[256];
        if (snprintf(rel, sizeof(rel), "/bin/%s", cmd) < (int)sizeof(rel) &&
            resolve_path(rel, path, sizeof(path)))
            image = load_image(path, &len, &owned);
        if (image < 0 && resolve_path(cmd, path, sizeof(path)))
            image = load_image(path, &len, &owned);
    }
    if (image < 0) {
        printf("zzsh: %s: command not found\n", cmd);
        return false;
    }

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT,
                      .max_prio = SPAWN_PRIO_INHERIT,
                      .stdin_pipe = in,
                      .stdout_pipe = out};
    Err rc = SpawnProcessObjAttr(image, len, name, argbuf, argpos, argc, &attr, pid_out, task_out);
    if (owned)
        HandleClose(image);
    else if (rc != ZUZU_OK)
        ImgCacheDrop(&g_images, path);
    if (rc != ZUZU_OK) {
        printf(ANSI_RED "zzsh: %s: spawn failed (err %d)\n" ANSI_RESET, cmd, rc);
        return false;
    }
    return true;
}

static void cmd_run_single(const char *line)
{
    stdio_close_tty();

    Spid pid;
    Handle task;
    if (!spawn_cmd(line, NULL, NULL, &task, &pid))
        return;

    WaitOn(task, TIMEOUT_INFINITE);
    HandleClose(task);
    HandleDestroy((Handle)pid);
}

static bool is_builtin(const char *text)
{
    while (*text == ' ')
        text++;
    static const char *const names[] = {"cd", "pwd", "clear", "help", "exit"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i]);
        if (strncmp(text, names[i], n) == 0 && (text[n] == ' ' || text[n] == '\0'))
            return true;
    }
    return false;
}

/* Runs `a | b | c`: every stage in its own Space, joined by pipes. The shell keeps both ends of
 * each pipe so it can tell a stage's neighbour when that stage is gone, whether it exited,
 * faulted or was killed. */
static void cmd_pipeline(const char *line)
{
    char stage[PIPELINE_MAX][LINE_BUFFER_SIZE];
    int n = 0;
    const char *p = line;
    for (;;) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (n == PIPELINE_MAX) {
            printf("%s", ANSI_RED "zzsh: too many stages in a pipeline\n" ANSI_RESET);
            return;
        }
        memcpy(stage[n], p, len);
        stage[n][len] = '\0';
        strip(stage[n]);
        if (!stage[n][0]) {
            printf("%s", ANSI_RED "zzsh: syntax error near '|'\n" ANSI_RESET);
            return;
        }
        if (is_builtin(stage[n])) {
            printf("%s", ANSI_RED "zzsh: builtins cannot be used in a pipeline\n" ANSI_RESET);
            return;
        }
        n++;
        if (!bar)
            break;
        p = bar + 1;
    }

    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "zzsh: fsd unavailable\n" ANSI_RESET);
        return;
    }

    PipeEnd pw[PIPELINE_MAX], pr[PIPELINE_MAX];
    int pipes = 0;
    for (; pipes < n - 1; pipes++) {
        if (CreatePipe(PIPE_SIZE_DEFAULT, &pw[pipes], &pr[pipes]) != ZUZU_OK) {
            printf("%s", ANSI_RED "zzsh: cannot create a pipe\n" ANSI_RESET);
            goto cleanup_pipes;
        }
    }

    stdio_close_tty();

    Handle task[PIPELINE_MAX];
    Spid pid[PIPELINE_MAX];
    int started = 0;
    for (; started < n; started++) {
        const PipeEnd *in = started > 0 ? &pr[started - 1] : NULL;
        const PipeEnd *out = started < n - 1 ? &pw[started] : NULL;
        if (!spawn_cmd(stage[started], in, out, &task[started], &pid[started]))
            break;
    }

    Handle ev = CreateEvent();
    unsigned live = 0;
    for (int i = 0; i < started; i++) {
        if (ev < 0 || Bind(EVENT_TASK, ev, task[i], (uint32_t)i) != ZUZU_OK) {
            HandleDestroy((Handle)pid[i]); /* cannot be watched: end it rather than hang */
            continue;
        }
        live |= 1u << i;
    }
    if (started < n) {
        /* A stage failed to start: the rest would wait on it forever. */
        for (int i = 0; i < started; i++)
            HandleDestroy((Handle)pid[i]);
    }

    while (live) {
        EventWaitResult r = WaitOnEvent(ev, TIMEOUT_INFINITE, 0, false);
        if (r.status != ZUZU_OK)
            break;
        for (int i = 0; i < started; i++) {
            if (!(live & (1u << i)) || !(r.bits & (1u << i)))
                continue;
            live &= ~(1u << i);
            if (i < n - 1)
                PipeMarkClosed(&pw[i], PIPE_WRITER);
            if (i > 0)
                PipeMarkClosed(&pr[i - 1], PIPE_READER);
        }
    }
    for (int i = 0; i < started; i++) {
        HandleClose(task[i]);
        HandleDestroy((Handle)pid[i]);
    }
    if (ev >= 0)
        HandleClose(ev);

cleanup_pipes:
    for (int i = 0; i < pipes; i++) {
        ClosePipe(&pw[i]);
        ClosePipe(&pr[i]);
    }
}

static void cmd_run(const char *line)
{
    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "zzsh: fsd unavailable\n" ANSI_RESET);
        return;
    }

    if (strchr(line, '|'))
        cmd_pipeline(line);
    else
        cmd_run_single(line);
}

/* ---- dispatch ---- */

static void cmd_help(void)
{
    printf("%s",
           ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET ANSI_BOLD "  help" ANSI_RESET
                               "          show this message\n" ANSI_BOLD "  clear" ANSI_RESET
                               "         clear the screen\n" ANSI_BOLD "  pwd" ANSI_RESET
                               "           print current directory\n" ANSI_BOLD
                               "  cd <path>" ANSI_RESET "     change current directory\n" ANSI_BOLD
                               "  ls [path]" ANSI_RESET "     list directory\n" ANSI_BOLD
                               "  cat <file>" ANSI_RESET "    print file contents\n" ANSI_BOLD
                               "  exit" ANSI_RESET "          leave the shell\n" ANSI_BOLD
                               "  <program>" ANSI_RESET "     run /bin/<program> or a path\n" ANSI_BOLD
                               "  a | b" ANSI_RESET "         pipe a's output into b\n");
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

    if (strchr(line, '|'))
        cmd_run(line);
    else if (strcmp(line, "help") == 0)
        cmd_help();
    else if (strcmp(line, "clear") == 0)
        printf("%s", ANSI_CLEAR);
    else if (strcmp(line, "pwd") == 0)
        printf("%s\n", cwd);
    else if (strcmp(line, "cd") == 0)
        cmd_cd(NULL);
    else if (strncmp(line, "cd ", 3) == 0)
        cmd_cd(line + 3);
    else
        cmd_run(line);

    return true;
}

static char history[HISTORY_MAX][LINE_BUFFER_SIZE];
static int hist_head; /* next write slot */
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

static void redraw_line(const char *line) { printf("\r%s%s\033[K", PROMPT, line); }

/* Raw mode: zzsh does its own echo, backspace and history. Returns false on EOF (^D on an empty
 * line). */
static bool read_line(char *line)
{
    enum { ST_NORMAL, ST_ESC, ST_CSI } state = ST_NORMAL;
    char saved[LINE_BUFFER_SIZE];
    size_t pos = 0;
    int hist_pos = 0;

    line[0] = '\0';
    for (;;) {
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

static void __attribute__((destructor)) ShellDetachFsd(void) { FsdDetach(&fsd_conn); }

int main(void)
{
    stdio_set_raw(1);
    if (stdio_open_tty() != 0)
        return 1;

    printf("%s", ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET);

    for (;;) {
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
