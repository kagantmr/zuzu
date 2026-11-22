#include <dev/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/stdio_core.h>
#include <zuzu/service.h>

/** @brief Set by ^C. */
static volatile int g_stop;

/** @brief ^C handler: ask the read loop to finish. */
static void OnInterrupt(void) { g_stop = 1; }

/** @brief Level names, indexed by LogLevel. */
static const char *const kLevels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

/**
 * @brief Parses a level name or number.
 * @param[in]  s   Text such as "warn" or "3".
 * @param[out] out The level.
 * @return 0 on success, -1 if @p s is not a level.
 */
static int ParseLevel(const char *s, int *out)
{
    for (int i = 0; i <= LOG_LEVEL_FATAL; i++) {
        const char *n = kLevels[i];
        size_t k = 0;
        while (n[k] && s[k] && ((n[k] | 0x20) == (s[k] | 0x20)))
            k++;
        if (n[k] == '\0' && s[k] == '\0') {
            *out = i;
            return 0;
        }
    }
    if (s[0] >= '0' && s[0] <= '5' && s[1] == '\0') {
        *out = s[0] - '0';
        return 0;
    }
    return -1;
}

/** @brief Prints the usage line. */
static void Usage(void) { (void)fputs("usage: logs [-f] [-l level] [-s source] [-n count]\n", stderr); }

int main(int argc, char *argv[])
{
    int level = 0;
    int follow = 0;
    long tail = -1;
    const char *source = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            follow = 1;
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            if (ParseLevel(argv[++i], &level) != 0) {
                Usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            source = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            tail = strtol(argv[++i], NULL, 10);
        } else {
            Usage();
            return 2;
        }
    }

    Handle port = LookupService("/svc/log");
    if (port < 0) {
        fputs("logs: logsvc is not running\n", stderr);
        return 1;
    }

    uint32_t from = 0;
    if (tail >= 0) {
        LogIndexRequest sreq = {.cmd = LOG_STATS};
        LogStatsReply st;
        if (LogCall(port, &sreq, sizeof(sreq), -1, NULL) == ZUZU_OK) {
            memcpy(&st, GetMessageBox(), sizeof(st));
            from = st.next_seq > (uint32_t)tail ? st.next_seq - (uint32_t)tail : 0;
        }
    }

    LogReader rd;
    Err rc = LogReaderOpen(port, from, (uint8_t)level, source, follow ? LOG_READ_FOLLOW : 0, &rd);
    if (rc != ZUZU_OK) {
        fprintf(stderr, "logs: cannot open a reader (%d)\n", rc);
        return 1;
    }

    StdioOnInterrupt(OnInterrupt);
    int status = 0;
    while (!g_stop) {
        LogEntryHdr h;
        char msg[LOG_MSG_MAX + 1];
        int n = LogReaderNext(&rd, &h, msg, 250);
        if (n == ERR_TIMEOUT || n == ERR_INTR)
            continue;
        if (n == 0)
            break;
        if (n < 0) {
            fprintf(stderr, "logs: read failed (%d)\n", n);
            status = 1;
            break;
        }
        msg[h.length] = '\0';
        char src[LOG_SOURCE_MAX + 1];
        memcpy(src, h.source, LOG_SOURCE_MAX);
        src[LOG_SOURCE_MAX] = '\0';
        printf("[%7u %-5s] (%s) %s\n", (unsigned)(h.timestamp_us / 1000U), kLevels[h.level], src,
               msg);
    }
    LogReaderClose(&rd);
    return status;
}
