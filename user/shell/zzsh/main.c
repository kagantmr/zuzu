#include "zzsh.h"
#include <ansi.h>
#include <mem.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <zuzu/service.h>
#include <malloc.h>
#include <zuzu/syspage.h>
#include <zuzu/fsd_client.h>
#include <zuzu/protocols/nic_protocol.h>
#include <zuzu/protocols/sysd_protocol.h>
#include <zuzu/channel.h>
#include <zuzu/user_layout.h>

static int32_t sysd_port;
static uint32_t sysd_pid;
static fsd_conn_t fsd_conn;   /* session with the filesystem daemon */
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

int setup(void)
{
    msg_t sysd = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack(NT_NAME_SYS), 0);
    if (sysd.r1 != NT_LU_OK)
        return -1;
    sysd_port = (int32_t)sysd.r2;
    sysd_pid = sysd.r3;

    if (stdio_use_tty(0) < 0)
        return -1;

    return 0;
}

static bool ensure_fsd(void)
{
    return fsd_connect(&fsd_conn, FSD_SHM_DEFAULT) == ZUZU_OK;
}

// Redraws prompt + current line content, clears to end of line.
// Used when history navigation changes what's displayed.
static void redraw_line(const char *line)
{
    printf("\r%s", PROMPT);
    printf("%s", line);
    printf("\033[K");
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

static bool stat_path(const char *path, fsd_stat_t *st)
{
    if (!ensure_fsd())
        return false;

    if (strlen(path) >= 4096)
        return false;

    return fsd_stat(&fsd_conn, path, st) == ZUZU_OK;
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return base;
}

static bool path_is_zzsh(const char *path)
{
    const char *base = path_basename(path);
    return strcmp(base, "zzsh") == 0 || strcmp(base, "zzsh.elf") == 0;
}

static void print_exec_error(int32_t code)
{
    switch (code) {
        case ERR_NOENT:
            printf("%s", ANSI_RED "zzsh: spawn failed (not found)\n" ANSI_RESET);
            break;
        case ERR_NOMEM:
            printf("%s", ANSI_RED "zzsh: spawn failed (out of memory)\n" ANSI_RESET);
            break;
        case EXEC_EBADELF:
            printf("%s", ANSI_RED "zzsh: spawn failed (bad ELF)\n" ANSI_RESET);
            break;
        case EXEC_EIO:
            printf("%s", ANSI_RED "zzsh: spawn failed (I/O error)\n" ANSI_RESET);
            break;
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "zzsh: spawn failed (err %d)\n", code);
            printf("%s%s%s", ANSI_RED, msg, ANSI_RESET);
            break;
        }
    }
}

static bool exec_reply_valid(const exec_reply_t *reply)
{
    if (reply->entry < USER_ELF_BASE || reply->entry >= USER_STACK_GUARD_VA)
        return false;

    if (reply->sp < USER_STACK_BASE || reply->sp > USER_STACK_TOP)
        return false;

    if ((reply->sp & 0x3u) != 0)
        return false;

    if (reply->argc == 0)
        return reply->argv_va == 0;

    if (reply->argv_va < USER_STACK_BASE || reply->argv_va >= USER_STACK_TOP)
        return false;

    if ((reply->argv_va & 0x3u) != 0)
        return false;

    return true;
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
        fsd_dirent_t entries[32];
        uint32_t count = 0;
        if (fsd_readdir(&fsd_conn, path, start, entries,
                        sizeof(entries) / sizeof(entries[0]), &count) != ZUZU_OK) {
            if (start == 0)
                printf("%s", ANSI_RED "ls: cannot read directory\n" ANSI_RESET);
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].type == FSD_TYPE_DIR) {
                snprintf(line, sizeof(line), ANSI_BOLD ANSI_CYAN "%-13s" ANSI_RESET "  <DIR>\n",
                         entries[i].name);
            } else {
                snprintf(line, sizeof(line), "%-13s  %u\n",
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
    if (fsd_open(&fsd_conn, abs_path, FSD_MODE_READ, &fd) != ZUZU_OK) {
        printf("%s", ANSI_RED "cat: file not found\n" ANSI_RESET);
        return;
    }

    char chunk[4096];
    while (1) {
        uint32_t got = 0;
        if (fsd_read(&fsd_conn, fd, chunk, sizeof(chunk) - 1, &got) != ZUZU_OK)
            break;
        if (got == 0) break;

        chunk[got] = '\0';  /* null-terminate for printf */
        printf("%s", chunk);
    }

    fsd_close(&fsd_conn, fd);
    printf("\n");
}

/* ---- exec from SD ---- */

static void cmd_exec(const char *line)
{
    if (!ensure_fsd()) {
        printf("%s", ANSI_RED "zzsh: fsd unavailable\n" ANSI_RESET);
        return;
    }

    /* ---- tokenize ---- */
    char buf[LINE_BUFFER_SIZE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[16];
    int token_count = 0;
    char *p = buf;
    while (*p && token_count < 16) {
        while (*p == ' ') p++;
        if (!*p) break;
        tokens[token_count++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (token_count == 0) return;

    /* ---- resolve path from first token ---- */
    char path[256];
    if (!resolve_path(tokens[0], path, sizeof(path))) {
        printf("%s", "zzsh: path too long\n");
        return;
    }

    if (path_is_zzsh(path)) {
        printf("%s", ANSI_RED "zzsh: refusing to spawn itself\n" ANSI_RESET);
        return;
    }

    /* ---- build argbuf: "tok0\0tok1\0tok2\0" ---- */
    char argbuf[512];
    size_t argpos = 0;
    for (int i = 0; i < token_count; i++) {
        size_t len = strlen(tokens[i]) + 1;
        if (argpos + len > sizeof(argbuf)) break;
        memcpy(argbuf + argpos, tokens[i], len);
        argpos += len;
    }

    /* ---- pspawn locally, ask sysd to inject, then kickstart ---- */
    const char *name = path_basename(path);
    tspawn_result_t ts = zuzu_pspawn(name);
    if (ts.task_handle < 0) {
        printf("%s", ANSI_RED "zzsh: spawn failed\n" ANSI_RESET);
        return;
    }

    int32_t sysd_task_handle = zuzu_grant(ts.task_handle, (int32_t)sysd_pid);
    if (sysd_task_handle < 0) {
        zuzu_pkill(ts.task_handle);                    /* <-- NEW */
        printf("%s", ANSI_RED "zzsh: spawn failed (sysd reject)\n" ANSI_RESET);
        return;
    }

    size_t path_len = strlen(path);
    size_t req_len = sizeof(exec_request_hdr_t) + path_len + 1 + argpos;
    if (req_len > LMSG_BUF_SIZE) {
        zuzu_pkill(ts.task_handle);                    /* <-- NEW */
        printf("%s", ANSI_RED "zzsh: command too long\n" ANSI_RESET);
        return;
    }

    exec_request_hdr_t *hdr = (exec_request_hdr_t *)lmsg_buf();
    hdr->cmd = SYSD_EXEC;
    hdr->_pad = 0;
    hdr->task_handle = (uint16_t)sysd_task_handle;
    hdr->path_len = (uint16_t)path_len;
    hdr->argc = (uint16_t)token_count;
    hdr->pid = ts.pid;

    char *payload = (char *)lmsg_buf() + sizeof(*hdr);
    memcpy(payload, path, path_len + 1);
    memcpy(payload + path_len + 1, argbuf, argpos);

    int32_t rc = chan_call((handle_t)sysd_port, lmsg_buf(), (uint32_t)req_len,
                           lmsg_buf(), (uint32_t)sizeof(exec_reply_t));
    if (rc < 0) {
        zuzu_pkill(ts.task_handle);
        print_exec_error(rc);
        return;
    }
    if (rc != (int32_t)sizeof(exec_reply_t)) {
        zuzu_pkill(ts.task_handle);
        printf("%s", ANSI_RED "zzsh: bad exec reply\n" ANSI_RESET);
        return;
    }

    exec_reply_t *reply = (exec_reply_t *)lmsg_buf();
    if (!exec_reply_valid(reply)) {
        zuzu_pkill(ts.task_handle);
        print_exec_error(EXEC_EBADELF);
        return;
    }

    if (zuzu_kickstart(ts.task_handle, reply->entry, reply->sp,
                   reply->argc, reply->argv_va) != 0) {
        zuzu_pkill(ts.task_handle);
        printf("%s", ANSI_RED "zzsh: kickstart failed\n" ANSI_RESET);
        return;
    }

    int32_t exit_status = 0;
    zuzu_wait(ts.pid, &exit_status, 0);
}

/* ---- dispatch ---- */

void command_dispatch(const char *line)
{
    if (strcmp(line, "help") == 0)
    {
        printf("%s",
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
        printf("%s", ANSI_CLEAR);
    }
    else if (strcmp(line, "free") == 0)
    {
        // read info page
        const syspage_t *sp = (const syspage_t *)0x1000;
        uint32_t fp = sp->mem_free_kb; // free pages (4KB each)
        char buf[64];
        snprintf(buf, sizeof(buf), "%u pages free (%u KB), %u pages full\n", fp / 4, fp, 16384 - fp/4);
        printf("%s", buf);
    }
    else if (strcmp(line, "pwd") == 0)
    {
        printf("%s", cwd);
        printf("\n");
    }
    else if (strcmp(line, "cd") == 0)
    {
        printf("%s", "usage: cd <path>\n");
    }
    else if (strncmp(line, "cd ", 3) == 0)
    {
        char path[256];
        fsd_stat_t st;

        if (!resolve_path(line + 3, path, sizeof(path))) {
            printf("%s", "cd: path too long\n");
            return;
        }

        if (strcmp(path, "/") == 0) {
            strncpy(cwd, "/", sizeof(cwd) - 1);
            cwd[sizeof(cwd) - 1] = '\0';
            return;
        }

        if (!stat_path(path, &st)) {
            printf("%s", ANSI_RED "cd: not found\n" ANSI_RESET);
            return;
        }

        if (st.type != FSD_TYPE_DIR) {
            printf("%s", ANSI_RED "cd: not a directory\n" ANSI_RESET);
            return;
        }

        strncpy(cwd, path, sizeof(cwd) - 1);
        cwd[sizeof(cwd) - 1] = '\0';
    }
    else if (strcmp(line, "pid") == 0)
    {
        uint32_t p = zuzu_getpid();
        char buf[32];
        snprintf(buf, sizeof(buf), "pid: %u\n", p);
        printf("%s", buf);
    }
    else if (strncmp(line, "sleep ", 6) == 0)
    {
        uint32_t ms = 0;
        const char *p = line + 6;
        while (*p >= '0' && *p <= '9')
            ms = ms * 10 + (uint32_t)(*p++ - '0');
        zuzu_sleep(ms);
    }
    else if (strncmp(line, "echo ", 5) == 0)
    {
        printf("%s", line + 5);
        printf("\n");
    }
    else if (strcmp(line, "nicstat") == 0)
    {
        int32_t nic_port = lookup_service("nic0");
        if (nic_port < 0) {
            printf("%s", "nicstat: nic0 not found\n");
        } else {
            msg_t r = zuzu_msg_call(nic_port, NIC_CMD_STATS, 0, 0);
            char buf[48];
            snprintf(buf, sizeof(buf), "nic0: irq_count=%u\n", (uint32_t)r.r2);
            printf("%s", buf);
        }
    }
    else if (strncmp(line, "ls", 2) == 0 && (line[2] == '\0' || line[2] == ' '))
    {
        const char *arg = (line[2] == ' ') ? line + 3 : NULL;
        cmd_ls(arg);
    }
    else if (strcmp(line, "cat") == 0)
    {
        printf("%s", "usage: cat <file>\n");
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

    if (stdio_register_uart() != 0)
        return 1;

    printf("%s", ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET);
    printf("%s", PROMPT);

    while (1)
    {
        int ch = getchar();
        if (ch == EOF) {
            zuzu_sleep(5);
            continue;
        }

        char c = (char)ch;

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
            printf("\r\n");
            line[pos] = '\0';
            strip(line);
            hist_pos = 0;
            if (line[0]) {
                hist_push(line);
                command_dispatch(line);
            }
            pos = 0;
            printf("\r\033[K");
            printf("%s", PROMPT);
        } else if (c == 127 || c == '\b') {
            if (pos > 0) {
                pos--;
                printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7f && pos < LINE_BUFFER_SIZE - 1) {
            line[pos++] = c;
            char echo[2] = { c, '\0' };
            printf("%s", echo);
        }
    }

    return 0;
}