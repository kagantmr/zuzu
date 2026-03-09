#include "zzsh.h"
#include <ansi.h>
#include <mem.h>
#include <string.h>
#include <snprintf.h>

static int32_t zuart_port;
static int32_t shmem_handle;
static char *shmem_buf;

void zprint(const char *s) {
    size_t len = strlen(s);
    if (len > 4096) len = 4096;
    memcpy(shmem_buf, s, len);
    zuzu_ipcmsg_t r = _call(zuart_port, ZUART_CMD_WRITE, shmem_handle, len);
}

size_t zread(char *dst, size_t max)
{
    if (max > 4096)
        max = 4096;
    zuzu_ipcmsg_t reply = _call(zuart_port, ZUART_CMD_READ, shmem_handle, max);
    size_t got = reply.r1;
    memcpy(dst, shmem_buf, got);
    return got;
}

void command_dispatch(const char *line)
{
    if (strcmp(line, "help") == 0)
    {
        zprint(ANSI_BOLD ANSI_BLUE "zzsh " ZZSH_VER "\n" ANSI_RESET
                                      "help          Show this message\n"
                                      "echo <text>   Print text to the terminal\n"
                                      "clear         Clear the screen\n"
                                      "free          Show free physical pages\n"
                                      "pid           Show shell PID\n"
                                      "<name>        Run /bin/<name> from initrd\n");
    }
    else if (strcmp(line, "clear") == 0)
    {
        zprint(ANSI_CLEAR);
    }
    else if (strcmp(line, "free") == 0)
    {
        uint32_t free_pages = _pmm_free();
        char free_str[25];
        snprintf(free_str, sizeof(free_str), "%u", free_pages);
        strcat(free_str, " pages are free\n");
        zprint(free_str);
    }
    else if (strcmp(line, "pid") == 0)
    {
        uint32_t zzsh_pid = _getpid();
        char pid_str[25];
        snprintf(pid_str, sizeof(pid_str), "%u", zzsh_pid);
        strcat(pid_str, " is ZZSH's pid\n");
        zprint(pid_str);
    }
    else if (strncmp(line, "echo ", 5) == 0)
    {
        zprint(line + 5);
        zprint("\n");
    }
    else
    {
        char path[LINE_BUFFER_SIZE + 4];
        snprintf(path, sizeof(path), "bin/%s", line);
        int32_t child = _spawn(path, strlen(path));
        if (child < 0)
        {
            zprint("zzsh: command not found\n");
        }
        else
        {
            int32_t status;
            _wait(child, &status);
        }
    }
}

int setup() {
    zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
    if (reply.r1 != NT_LU_OK) {
        return -1;
    }
    zuart_port = reply.r2;
    int32_t zuart_pid = reply.r3;

    shmem_result_t shm = _memshare(4096);
    if (shm.handle < 0) {
        return -1;
    }
    shmem_handle = shm.handle;
    shmem_buf = (char *)shm.addr;

    int32_t remote = _port_grant(shmem_handle, zuart_pid);
    if (remote < 0) {
        return -1;
    }
    shmem_handle = remote;

    return 0;
}

int main(void)
{
    if (setup() < 0)
    {
        return 1;
    }
    char line[LINE_BUFFER_SIZE];
    size_t pos;
    char tmp[64];

    zprint(ANSI_BOLD ANSI_BLUE "zzsh " ZZSH_VER "\n" ANSI_RESET);

    while (1)
    {
        zprint(ANSI_YELLOW "zzsh" ANSI_RESET "~>");
        pos = 0;

        while (1)
        {
            size_t n = zread(tmp, sizeof(tmp));
            for (size_t i = 0; i < n; i++)
            {
                char c = tmp[i];
                if (c == '\r' || c == '\n')
                {
                    zprint("\n");
                    goto line_done;
                }
                else if (c == 127 || c == '\b')
                {
                    if (pos > 0)
                    {
                        pos--;
                        zprint("\b \b");
                    }
                }
                else if (pos < LINE_BUFFER_SIZE - 1)
                {
                    line[pos++] = c;
                }
            }
        }
    line_done:
        line[pos] = '\0';

        if (pos == 0)
            continue;

        command_dispatch(line);
    }

    return 0;
}