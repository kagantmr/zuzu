#include "zzsh.h"
#include <ansi.h>
#include <mem.h>
#include <string.h>
#include <snprintf.h>

static int32_t zuart_port;
static int32_t shmem_handle;
static char   *shmem_buf;

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
    if (len > 4096) len = 4096;
    memcpy(shmem_buf, s, len);
    _call(zuart_port, ZUART_CMD_WRITE, zuart_pack_arg(shmem_handle, (uint32_t)len), 0);
}

size_t zread(char *dst, size_t max)
{
    if (max > 4096) max = 4096;
    zuzu_ipcmsg_t reply = _call(zuart_port, ZUART_CMD_READ, zuart_pack_arg(shmem_handle, (uint32_t)max), 0);
    size_t got = reply.r1;
    memcpy(dst, shmem_buf, got);
    return got;
}

// Redraws prompt + current line content, clears to end of line.
// Used when history navigation changes what's displayed.
static void redraw_line(const char *line)
{
    zprint("\r" PROMPT);
    zprint(line);
    zprint("\033[K");
}

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
            ANSI_BOLD "  pid" ANSI_RESET "           show shell PID\n"
            ANSI_BOLD "  dump" ANSI_RESET "          kernel debug dump\n"
            ANSI_BOLD "  sleep <ms>" ANSI_RESET "    sleep for <ms> milliseconds\n"
            ANSI_BOLD "  <name>" ANSI_RESET "        run bin/<name> from initrd\n"
        );
    }
    else if (strcmp(line, "clear") == 0)
    {
        zprint(ANSI_CLEAR);
    }
    else if (strcmp(line, "free") == 0)
    {
        uint32_t fp = _pmm_free();
        char buf[64];
        snprintf(buf, sizeof(buf), "%u pages free (%u KB)\n", fp, fp * 4);
        zprint(buf);
    }
    else if (strcmp(line, "pid") == 0)
    {
        uint32_t p = _getpid();
        char buf[32];
        snprintf(buf, sizeof(buf), "pid: %u\n", p);
        zprint(buf);
    }
    else if (strcmp(line, "dump") == 0)
    {
        _dump();
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
    else
    {
        char path[LINE_BUFFER_SIZE + 8];
        snprintf(path, sizeof(path), "bin/%s", line);
        int32_t child = _spawn(path, strlen(path));
        if (child < 0)
        {
            zprint(ANSI_RED "zzsh: unknown command: " ANSI_RESET);
            zprint(line);
            zprint("\n");
        }
        else
        {
            int32_t status;
            _wait(child, &status, 0);
        }
    }
}

int setup(void)
{
    zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
    if (reply.r1 != NT_LU_OK) return -1;
    zuart_port = reply.r2;
    int32_t zuart_pid = reply.r3;

    shmem_result_t shm = _memshare(4096);
    if (shm.handle < 0) return -1;
    shmem_handle = shm.handle;
    shmem_buf    = (char *)shm.addr;

    int32_t remote = _port_grant(shmem_handle, zuart_pid);
    if (remote < 0) return -1;
    shmem_handle = remote;

    return 0;
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
                zprint("\n");
                line[pos] = '\0';
                strip(line);
                hist_pos = 0;
                if (line[0]) {
                    hist_push(line);
                    command_dispatch(line);
                }
                pos = 0;
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
