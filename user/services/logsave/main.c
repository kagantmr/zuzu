#include <dev/log.h>
#include <fs/fsd_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_PATH "/system.log"
#define MAX_FILE_BYTES (4u * 1024u * 1024u)
#define BUF_BYTES (16u * 1024u)
#define FLUSH_BYTES (8u * 1024u)
#define FLUSH_MS 5000u
#define RETRY_MS 2000u
#define LINE_MAX (LOG_MSG_MAX + 64u)

static const char *const kLevels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static FsdConn g_fsd;
static char g_buf[BUF_BYTES];
static uint32_t g_len;
static uint32_t g_dropped;
static bool g_started;
static uint64_t g_last_flush_ms;
static uint64_t g_last_try_ms;
static uint32_t g_next_seq;

static bool FsReady(void)
{
    if (g_fsd.ready)
        return true;
    uint64_t now = UptimeMs();
    if (g_last_try_ms && now - g_last_try_ms < RETRY_MS)
        return false;
    g_last_try_ms = now;
    return FsdConnect(&g_fsd, FSD_SHM_MIN * 4) == ZUZU_OK;
}

static uint32_t StartMode(void)
{
    FsdStat st;
    if (FsdGetStat(&g_fsd, LOG_PATH, &st) == ZUZU_OK && st.size > MAX_FILE_BYTES)
        return FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS;
    return FSD_MODE_WRITE | FSD_MODE_OPEN_APPEND;
}

static void Flush(void)
{
    if (g_len == 0 || !FsReady())
        return;
    uint32_t mode = FSD_MODE_WRITE | FSD_MODE_OPEN_APPEND;
    if (!g_started)
        mode = StartMode();
    uint32_t fd;
    if (FsdOpen(&g_fsd, LOG_PATH, mode, &fd) != ZUZU_OK)
        return;
    uint32_t done = 0;
    while (done < g_len) {
        uint32_t put = 0;
        if (FsdWrite(&g_fsd, fd, g_buf + done, g_len - done, &put) != ZUZU_OK || put == 0)
            break;
        done += put;
    }
    FsdClose(&g_fsd, fd);
    g_started = true;
    memmove(g_buf, g_buf + done, g_len - done);
    g_len -= done;
    g_last_flush_ms = UptimeMs();
}

static void Append(const char *line, uint32_t n)
{
    if (BUF_BYTES - g_len < n) {
        g_dropped++;
        return;
    }
    memcpy(g_buf + g_len, line, n);
    g_len += n;
}

static void AppendRecord(const LogEntryHdr *h, const char *msg)
{
    char src[LOG_SOURCE_MAX + 1];
    memcpy(src, h->source, LOG_SOURCE_MAX);
    src[LOG_SOURCE_MAX] = '\0';
    char line[LINE_MAX];
    int n = snprintf(line, sizeof(line), "[%7u %-5s] (%s) %.*s\n",
                     (unsigned)(h->timestamp_us / 1000U),
                     h->level <= LOG_LEVEL_FATAL ? kLevels[h->level] : "?", src, (int)h->length,
                     msg);
    if (n <= 0)
        return;
    Append(line, (uint32_t)n < sizeof(line) ? (uint32_t)n : (uint32_t)sizeof(line) - 1);
}

static Err OpenReader(uint8_t min_level, LogReader *rd)
{
    for (;;) {
        Handle port = LookupService("/svc/log");
        if (port >= 0) {
            Err rc = LogReaderOpen(port, g_next_seq, min_level, NULL, LOG_READ_FOLLOW, rd);
            HandleClose(port);
            if (rc == ZUZU_OK)
                return ZUZU_OK;
        }
        Sleep(RETRY_MS);
    }
}

int main(int argc, char **argv)
{
    uint8_t min_level = LOG_LEVEL_INFO;
    if (argc > 1) {
        long v = strtol(argv[1], NULL, 10);
        if (v >= LOG_LEVEL_TRACE && v <= LOG_LEVEL_FATAL)
            min_level = (uint8_t)v;
    }

    char marker[64];
    int m = snprintf(marker, sizeof(marker), "---- boot, logsave up at %llu ms ----\n",
                     (unsigned long long)UptimeMs());
    if (m > 0)
        Append(marker, (uint32_t)m);

    LogReader rd;
    OpenReader(min_level, &rd);
    g_last_flush_ms = UptimeMs();

    for (;;) {
        LogEntryHdr h;
        char msg[LOG_MSG_MAX];
        int n = LogReaderNext(&rd, &h, msg, 1000);
        if (n == 1) {
            g_next_seq = h.seq + 1;
            if (g_dropped) {
                char note[48];
                int k = snprintf(note, sizeof(note), "---- logsave dropped %u lines ----\n",
                                 (unsigned)g_dropped);
                g_dropped = 0;
                if (k > 0)
                    Append(note, (uint32_t)k);
            }
            AppendRecord(&h, msg);
            if (h.level >= LOG_LEVEL_ERROR)
                Flush();
        } else if (n != ERR_TIMEOUT && n != ERR_INTR) {
            LogReaderClose(&rd);
            OpenReader(min_level, &rd);
        }
        if (g_len >= FLUSH_BYTES || (g_len && UptimeMs() - g_last_flush_ms >= FLUSH_MS))
            Flush();
    }
}
