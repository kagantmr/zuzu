#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "speedtest.h"

#define OUT_BUF_SIZE (1024u * 1024u)
#define OUT_SLACK 1024u

static FsdConn g_fsd;
static bool g_fsd_tried;
static bool g_enabled;
static bool g_raw;
static uint32_t g_fd;
static char *g_buf;
static uint32_t g_len;
static uint64_t g_written;
static char g_section[32] = "init";

FsdConn *FsConn(void)
{
    if (!g_fsd_tried) {
        g_fsd_tried = true;
        if (FsdConnect(&g_fsd, FSD_SHM_DEFAULT) != ZUZU_OK)
            printf("speedtest: fsd unavailable\n");
    }
    return g_fsd.ready ? &g_fsd : NULL;
}

int OutOpen(const char *path, bool raw)
{
    FsdConn *c = FsConn();
    if (!c)
        return -1;
    void *buf = MemMapAnon(OUT_BUF_SIZE, 0, PROT_RW);
    if (PtrIsErr(buf)) {
        printf("speedtest: no memory for output buffer\n");
        return -1;
    }
    Err rc = FsdOpen(c, path, FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS, &g_fd);
    if (rc != ZUZU_OK) {
        printf("speedtest: cannot create %s: %d\n", path, (int)rc);
        MemUnmap(buf);
        return -1;
    }
    g_buf = buf;
    for (uint32_t o = 0; o < OUT_BUF_SIZE; o += PAGE)
        g_buf[o] = 0;
    g_len = 0;
    g_raw = raw;
    g_enabled = true;
    return 0;
}

bool OutEnabled(void) { return g_enabled; }

bool OutRaw(void) { return g_raw; }

const char *OutSectionName(void) { return g_section; }

void OutFlush(void)
{
    if (!g_enabled || g_len == 0)
        return;
    uint32_t done = 0;
    while (done < g_len) {
        uint32_t put = 0;
        Err rc = FsdWrite(&g_fsd, g_fd, g_buf + done, g_len - done, &put);
        if (rc != ZUZU_OK || put == 0) {
            printf("speedtest: write failed (%d), output disabled\n", (int)rc);
            g_enabled = false;
            break;
        }
        done += put;
    }
    g_written += done;
    g_len = 0;
}

static void VAppend(const char *fmt, va_list ap)
{
    if (OUT_BUF_SIZE - g_len < OUT_SLACK)
        OutFlush();
    if (!g_enabled)
        return;
    int n = vsnprintf(g_buf + g_len, OUT_BUF_SIZE - g_len, fmt, ap);
    if (n < 0)
        return;
    if ((uint32_t)n >= OUT_BUF_SIZE - g_len)
        n = (int)(OUT_BUF_SIZE - g_len - 1);
    g_len += (uint32_t)n;
}

void OutPrintf(const char *fmt, ...)
{
    if (!g_enabled)
        return;
    va_list ap;
    va_start(ap, fmt);
    VAppend(fmt, ap);
    va_end(ap);
}

void OutSection(const char *name)
{
    strncpy(g_section, name, sizeof(g_section) - 1);
    OutPrintf("#section,%s\n", name);
}

void OutMeta(const char *key, const char *fmt, ...)
{
    if (!g_enabled)
        return;
    OutPrintf("M,%s,%s,", g_section, key);
    va_list ap;
    va_start(ap, fmt);
    VAppend(fmt, ap);
    va_end(ap);
    OutPrintf("\n");
}

void OutClose(void)
{
    if (!g_enabled)
        return;
    OutFlush();
    FsdClose(&g_fsd, g_fd);
    printf("speedtest: wrote %llu bytes\n", (unsigned long long)g_written);
    g_enabled = false;
    MemUnmap(g_buf);
    g_buf = NULL;
}
