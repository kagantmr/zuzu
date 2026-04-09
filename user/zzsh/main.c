#include "zzsh.h"
#include <ansi.h>
#include <mem.h>
#include <string.h>
#include <snprintf.h>
#include <stdbool.h>
#include <service.h>
#include <zmalloc.h>
#include <zuzu/syspage.h>
#include <zuzu/protocols/fbox_protocol.h>

static int32_t zuart_port;
static int32_t fbox_port;
static char   *fbox_buf;   /* shmem shared with fbox */
static char cwd[256] = "/";

// -------------------- Prompt --------------------
#define PROMPT \
    ANSI_BOLD ANSI_GREEN "zzsh" ANSI_RESET \
    ANSI_CYAN " ~>" ANSI_RESET " "

// -------------------- History --------------------
static char history[HISTORY_MAX][LINE_BUFFER_SIZE];
static int  hist_head  = 0; // next write slot
static int  hist_count = 0; // entries stored (capped at HISTORY_MAX)

static void hist_push(const char *line)
{
    if (!line[0]) return;
    int last = (hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (hist_count > 0 && strcmp(history[last], line) == 0)
        return;
    strncpy(history[hist_head], line, LINE_BUFFER_SIZE - 1);
    history[hist_head][LINE_BUFFER_SIZE - 1] = '\0';
    hist_head = (hist_head + 1) % HISTORY_MAX;
    if (hist_count < HISTORY_MAX) hist_count++;
}

// offset 1 = most recent, 2 = one before that, etc.
static const char *hist_get(int offset)
{
    if (offset < 1 || offset > hist_count) return NULL;
    return history[(hist_head - offset + HISTORY_MAX * 2) % HISTORY_MAX];
}

// -------------------------------------------------

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

void zprint(const char *s)
{
    size_t len = strlen(s);
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;
    memcpy((void *)IPCX_BUF_VA, s, len);
    _sendx(zuart_port, (uint32_t)len);
}

size_t zread(char *dst, size_t max)
{
    if (max > IPCX_BUF_SIZE) max = IPCX_BUF_SIZE;
    zuzu_ipcmsg_t reply = _callx(zuart_port, (uint32_t)max);
    if (reply.r0 < 0) {
        return 0;
    }

    size_t got = reply.r1;
    if (got > max) got = max;
    memcpy(dst, (void *)IPCX_BUF_VA, got);
    return got;
}

int setup(void)
{
    zuart_port = lookup_service("uart");
    if (zuart_port < 0) return -1;

    fbox_port = lookup_service("fbox");
    if (fbox_port < 0) return -1;

    zuzu_ipcmsg_t r = _call(fbox_port, FBOX_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) return -1;

    fbox_buf = (char *)_attach((int32_t)r.r2);
    if ((intptr_t)fbox_buf <= 0) return -1;

    return 0;
}

// Redraws prompt + current line content, clears to end of line.
// Used when history navigation changes what's displayed.
static void redraw_line(const char *line)
{
    zprint("\r" PROMPT);
    zprint(line);
    zprint("\033[K");
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

static bool stat_path(const char *path, fbox_stat_t *st)
{
    size_t plen = strlen(path);
    if (plen >= 4096)
        return false;

    memcpy(fbox_buf, path, plen + 1);
    zuzu_ipcmsg_t r = _call(fbox_port, FBOX_STAT, 0, 0);
    if ((int32_t)r.r1 != FBOX_OK)
        return false;

    memcpy(st, fbox_buf, sizeof(*st));
    return true;
}

/* ---- ls ---- */

static void cmd_ls(const char *arg)
{
    char path[256];
    if (!resolve_path(arg, path, sizeof(path))) {
        zprint("path too long\n");
        return;
    }

    size_t plen = strlen(path);
    memcpy(fbox_buf, path, plen + 1);

    zuzu_ipcmsg_t r = _call(fbox_port, FBOX_READDIR, 0, 0);
    if ((int32_t)r.r1 != FBOX_OK) {
        zprint(ANSI_RED "ls: cannot read directory\n" ANSI_RESET);
        return;
    }

    uint32_t count = r.r2;
    fbox_dirent_t *entries = (fbox_dirent_t *)fbox_buf;
    char line[96];

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].is_dir) {
            snprintf(line, sizeof(line), ANSI_BOLD ANSI_CYAN "%-13s" ANSI_RESET "  <DIR>\n",
                     entries[i].name);
        } else {
            snprintf(line, sizeof(line), "%-13s  %u\n",
                     entries[i].name, entries[i].size);
        }
        zprint(line);
    }
}

/* ---- cat ---- */

static void cmd_cat(const char *path)
{
    if (!path || !path[0]) {
        zprint("usage: cat <file>\n");
        return;
    }

    char abs_path[256];
    if (!resolve_path(path, abs_path, sizeof(abs_path))) {
        zprint("cat: path too long\n");
        return;
    }

    size_t plen = strlen(abs_path);
    memcpy(fbox_buf, abs_path, plen + 1);

    zuzu_ipcmsg_t r = _call(fbox_port, FBOX_OPEN, FAT32_MODE_READ, 0);
    if ((int32_t)r.r1 != FBOX_OK) {
        zprint(ANSI_RED "cat: file not found\n" ANSI_RESET);
        return;
    }
    uint32_t fd = r.r2;

    while (1) {
        r = _call(fbox_port, FBOX_READ, FBOX_PACK_RW(fd, 4095), 0);
        if ((int32_t)r.r1 != FBOX_OK) break;
        uint32_t got = r.r2;
        if (got == 0) break;

        /* null-terminate for zprint */
        fbox_buf[got] = '\0';
        zprint(fbox_buf);
    }

    _call(fbox_port, FBOX_CLOSE, fd, 0);
    zprint("\n");
}

/* ---- exec from SD ---- */

static void cmd_exec(const char *name)
{
    char path[256];
    if (!resolve_path(name, path, sizeof(path))) {
        zprint("zzsh: path too long\n");
        return;
    }

    /* stat first to get file size */
    size_t plen = strlen(path);
    memcpy(fbox_buf, path, plen + 1);

    zuzu_ipcmsg_t r = _call(fbox_port, FBOX_STAT, 0, 0);
    if ((int32_t)r.r1 != FBOX_OK) {
        zprint(ANSI_RED "zzsh: not found: " ANSI_RESET);
        zprint(name);
        zprint("\n");
        return;
    }

    fbox_stat_t *st = (fbox_stat_t *)fbox_buf;
    uint32_t file_size = st->size;

    if (file_size == 0 || st->is_dir) {
        zprint(ANSI_RED "zzsh: not an executable\n" ANSI_RESET);
        return;
    }

    /* open the file */
    memcpy(fbox_buf, path, plen + 1);
    r = _call(fbox_port, FBOX_OPEN, FAT32_MODE_READ, 0);
    if ((int32_t)r.r1 != FBOX_OK) {
        zprint(ANSI_RED "zzsh: cannot open file\n" ANSI_RESET);
        return;
    }
    uint32_t fd = r.r2;

    /* allocate buffer for the full ELF */
    uint8_t *elf = (uint8_t *)zmalloc(file_size);
    if (!elf) {
        zprint(ANSI_RED "zzsh: out of memory\n" ANSI_RESET);
        _call(fbox_port, FBOX_CLOSE, fd, 0);
        return;
    }

    /* read the entire file */
    uint32_t total = 0;
    while (total < file_size) {
        uint32_t chunk = file_size - total;
        if (chunk > 4096) chunk = 4096;

        r = _call(fbox_port, FBOX_READ, FBOX_PACK_RW(fd, chunk), 0);
        if ((int32_t)r.r1 != FBOX_OK || r.r2 == 0) break;

        memcpy(elf + total, fbox_buf, r.r2);
        total += r.r2;
    }

    _call(fbox_port, FBOX_CLOSE, fd, 0);

    if (total < file_size) {
        zprint(ANSI_RED "zzsh: short read\n" ANSI_RESET);
        zfree(elf);
        return;
    }

    /* spawn */
    int32_t child = _spawn(elf, total, name, strlen(name));
    zfree(elf);

    if (child < 0) {
        zprint(ANSI_RED "zzsh: spawn failed\n" ANSI_RESET);
        return;
    }

    int32_t status;
    _wait(child, &status, 0);
}

/* ---- dispatch ---- */

void command_dispatch(const char *line)
{
    if (strcmp(line, "help") == 0)
    {
        zprint(
            ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET
            ANSI_BOLD "  help" ANSI_RESET "          show this message\n"
            ANSI_BOLD "  echo <text>" ANSI_RESET "   print text\n"
            ANSI_BOLD "  clear" ANSI_RESET "         clear the screen\n"
            ANSI_BOLD "  free" ANSI_RESET "          show free physical pages\n"
            ANSI_BOLD "  pwd" ANSI_RESET "           print current directory\n"
            ANSI_BOLD "  cd <path>" ANSI_RESET "     change current directory\n"
            ANSI_BOLD "  pid" ANSI_RESET "           show shell PID\n"
            ANSI_BOLD "  sleep <ms>" ANSI_RESET "    sleep for <ms> milliseconds\n"
            ANSI_BOLD "  ls [path]" ANSI_RESET "     list directory on SD\n"
            ANSI_BOLD "  cat <file>" ANSI_RESET "    print file contents from SD\n"
            ANSI_BOLD "  <path>" ANSI_RESET "        run executable from SD\n"
        );
    }
    else if (strcmp(line, "clear") == 0)
    {
        zprint(ANSI_CLEAR);
    }
    else if (strcmp(line, "free") == 0)
    {
        // read info page
        const zuzu_syspage_t *sp = (const zuzu_syspage_t *)0x1000;
        uint32_t fp = sp->mem_free_kb; // free pages (4KB each)
        char buf[64];
        snprintf(buf, sizeof(buf), "%u pages free (%u KB)\n", fp / 4, fp);
        zprint(buf);
    }
    else if (strcmp(line, "pwd") == 0)
    {
        zprint(cwd);
        zprint("\n");
    }
    else if (strcmp(line, "cd") == 0)
    {
        zprint("usage: cd <path>\n");
    }
    else if (strncmp(line, "cd ", 3) == 0)
    {
        char path[256];
        fbox_stat_t st;

        if (!resolve_path(line + 3, path, sizeof(path))) {
            zprint("cd: path too long\n");
            return;
        }

        if (strcmp(path, "/") == 0) {
            strncpy(cwd, "/", sizeof(cwd) - 1);
            cwd[sizeof(cwd) - 1] = '\0';
            return;
        }

        if (!stat_path(path, &st)) {
            zprint(ANSI_RED "cd: not found\n" ANSI_RESET);
            return;
        }

        if (!st.is_dir) {
            zprint(ANSI_RED "cd: not a directory\n" ANSI_RESET);
            return;
        }

        strncpy(cwd, path, sizeof(cwd) - 1);
        cwd[sizeof(cwd) - 1] = '\0';
    }
    else if (strcmp(line, "pid") == 0)
    {
        uint32_t p = _getpid();
        char buf[32];
        snprintf(buf, sizeof(buf), "pid: %u\n", p);
        zprint(buf);
    }
    else if (strncmp(line, "sleep ", 6) == 0)
    {
        uint32_t ms = 0;
        const char *p = line + 6;
        while (*p >= '0' && *p <= '9')
            ms = ms * 10 + (uint32_t)(*p++ - '0');
        _sleep(ms);
    }
    else if (strncmp(line, "echo ", 5) == 0)
    {
        zprint(line + 5);
        zprint("\n");
    }
    else if (strncmp(line, "ls", 2) == 0 && (line[2] == '\0' || line[2] == ' '))
    {
        const char *arg = (line[2] == ' ') ? line + 3 : NULL;
        cmd_ls(arg);
    }
    else if (strcmp(line, "cat") == 0)
    {
        zprint("usage: cat <file>\n");
    }
    else if (strncmp(line, "cat ", 4) == 0)
    {
        cmd_cat(line + 4);
    }
    else
    {
        cmd_exec(line);
    }
}

int main(void)
{
    if (setup() < 0) return 1;

    char line[LINE_BUFFER_SIZE];
    line[0] = '\0';
    char saved[LINE_BUFFER_SIZE]; // live edit saved during history browse
    size_t pos   = 0;
    int hist_pos = 0; // 0 = live edit, >0 = offset into history

    // Escape sequence parser state
    typedef enum { ST_NORMAL, ST_ESC, ST_CSI } input_state_t;
    input_state_t state = ST_NORMAL;

    char tmp[64];

    zprint(ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET);
    zprint(PROMPT);

    while (1)
    {
        size_t n = zread(tmp, sizeof(tmp));
        for (size_t i = 0; i < n; i++)
        {
            char c = tmp[i];

            // --- escape sequence state machine ---
            if (state == ST_ESC) {
                if (c == '[') { state = ST_CSI; continue; }
                state = ST_NORMAL;
            } else if (state == ST_CSI) {
                state = ST_NORMAL;
                if (c == 'A') {
                    // up arrow: go back in history
                    if (hist_pos == 0) {
                        memcpy(saved, line, pos);
                        saved[pos] = '\0';
                    }
                    if (hist_pos < hist_count) {
                        hist_pos++;
                        const char *h = hist_get(hist_pos);
                        if (h) {
                            strncpy(line, h, LINE_BUFFER_SIZE - 1);
                            line[LINE_BUFFER_SIZE - 1] = '\0';
                            pos = strlen(line);
                            redraw_line(line);
                        }
                    }
                } else if (c == 'B') {
                    // down arrow: go forward in history
                    if (hist_pos > 0) {
                        hist_pos--;
                        if (hist_pos == 0) {
                            strncpy(line, saved, LINE_BUFFER_SIZE - 1);
                            line[LINE_BUFFER_SIZE - 1] = '\0';
                            pos = strlen(line);
                        } else {
                            const char *h = hist_get(hist_pos);
                            if (h) {
                                strncpy(line, h, LINE_BUFFER_SIZE - 1);
                                line[LINE_BUFFER_SIZE - 1] = '\0';
                                pos = strlen(line);
                            }
                        }
                        redraw_line(line);
                    }
                }
                // left/right/other CSI sequences ignored
                continue;
            }

            if (c == '\033') { state = ST_ESC; continue; }

            // --- normal character handling ---
            if (c == '\r' || c == '\n') {
                zprint("\r\n");
                line[pos] = '\0';
                strip(line);
                hist_pos = 0;
                if (line[0]) {
                    hist_push(line);
                    command_dispatch(line);
                }
                pos = 0;
                zprint("\r\033[K");
                zprint(PROMPT);
            } else if (c == 127 || c == '\b') {
                if (pos > 0) {
                    pos--;
                    zprint("\b \b");
                }
            } else if (c >= 0x20 && c < 0x7f && pos < LINE_BUFFER_SIZE - 1) {
                line[pos++] = c;
                char echo[2] = { c, '\0' };
                zprint(echo);
            }
        }
    }

    return 0;
}
