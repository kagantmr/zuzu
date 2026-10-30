#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/uptime.h>

#include "speedtest.h"

typedef struct {
    const char *name;
    void (*fn)(void);
    const char *desc;
} Section;

static const Section kSections[] = {
    {"timer", BenchTimer, "cycle counter and uptime read cost"},
    {"syscall", BenchNullSyscall, "null syscall, valid handle query"},
    {"yield", BenchYield, "yield with nothing else runnable"},
    {"sleep", BenchSleep, "sleep and timeout accuracy (overshoot)"},
    {"handle", BenchHandleOps, "duplicate, close, signal, restrict"},
    {"hscale", BenchHandleScaling, "handle op cost vs handles held"},
    {"objects", BenchCreateObjects, "create/destroy port, event, mem"},
    {"memmap", BenchMemMap, "map, fault, unmap, protect"},
    {"tasks", BenchTasks, "create, start, kill tasks"},
    {"spaces", BenchSpaces, "create/destroy spaces, inject"},
    {"sched", BenchSched, "context switch, event ping-pong, wake latency"},
    {"ipc", RunIpc, "same-space IPC size sweep"},
    {"ipcx", RunIpcCross, "cross-space IPC size sweep"},
    {"membw", BenchMemBandwidth, "read/write/copy bandwidth vs size"},
    {"memlat", BenchMemLatency, "load latency vs working set"},
    {"noise", BenchNoise, "spin loop interruption trace"},
    {"fs", BenchFs, "SD card file throughput via fsd"},
};

static bool g_kstats = true;

static bool InList(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0)
            return true;
        if (!e)
            break;
        p = e + 1;
    }
    return false;
}

static void Usage(void)
{
    printf("usage: speedtest [-o FILE] [-r] [-n N] [-s LIST] [-x LIST] [-t MS] [-K] [-l]\n"
           "  -o FILE  save results as CSV on the SD card (graph with scripts/speedtest_plot.py)\n"
           "  -r       also save every raw sample (large file)\n"
           "  -n N     samples per microbenchmark (16..%u, default %u)\n"
           "  -s LIST  run only these sections (comma-separated)\n"
           "  -x LIST  skip these sections\n"
           "  -t MS    noise spin duration (default 2000)\n"
           "  -K       skip kernel bench counter dumps\n"
           "  -l       list sections\n",
           SAMPLES, SAMPLES);
}

static void RunSection(const Section *s)
{
    OutSection(s->name);
    printf("== %s: %s\n", s->name, s->desc);
    uint32_t khz0 = MeasureKhz(20, 3);
    uint64_t t0 = UptimeUs();
    BenchReset();
    s->fn();
    uint64_t dt = UptimeUs() - t0;
    uint32_t khz1 = MeasureKhz(20, 3);
    uint32_t d0 = khz0 > g_khz ? khz0 - g_khz : g_khz - khz0;
    uint32_t d1 = khz1 > g_khz ? khz1 - g_khz : g_khz - khz1;
    if ((uint64_t)(d0 > d1 ? d0 : d1) * 200 > g_khz)
        printf("!! clock drift in %s: %u -> %u kHz (calibrated %u), cycle figures suspect\n",
               s->name, (unsigned)khz0, (unsigned)khz1, (unsigned)g_khz);
    OutMeta("clock_khz_before", "%u", (unsigned)khz0);
    OutMeta("clock_khz_after", "%u", (unsigned)khz1);
    OutMeta("duration_ms", "%llu", (unsigned long long)(dt / 1000));
    OutFlush();
#ifdef CONFIG_ZUZU_BENCH
    if (g_kstats) {
        printf("-- kernel stats: %s\n", s->name);
        Sleep(250);
        BenchDump();
        Sleep(250);
    }
#endif
}

static char *Absolute(const char *p, char *buf, size_t n)
{
    if (p[0] == '/')
        return (char *)p;
    snprintf(buf, n, "/%s", p);
    return buf;
}

int main(int argc, char **argv)
{
    if (stdio_open_tty() != 0)
        return 1;

    const char *out = NULL;
    const char *only = NULL;
    const char *skip = NULL;
    bool raw = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-o") && v) {
            out = v;
            i++;
        } else if (!strcmp(a, "-r")) {
            raw = true;
        } else if (!strcmp(a, "-n") && v) {
            long n = strtol(v, NULL, 10);
            g_n = n < 16 ? 16 : n > SAMPLES ? SAMPLES : (uint32_t)n;
            g_warmup = g_n / 8;
            i++;
        } else if (!strcmp(a, "-s") && v) {
            only = v;
            i++;
        } else if (!strcmp(a, "-x") && v) {
            skip = v;
            i++;
        } else if (!strcmp(a, "-t") && v) {
            long t = strtol(v, NULL, 10);
            g_noise_ms = t < 10 ? 10 : t > 4000 ? 4000 : (uint32_t)t;
            i++;
        } else if (!strcmp(a, "-K")) {
            g_kstats = false;
        } else if (!strcmp(a, "-l")) {
            for (uint32_t k = 0; k < ARRAY_LEN(kSections); k++)
                printf("  %-8s %s\n", kSections[k].name, kSections[k].desc);
            return 0;
        } else {
            Usage();
            return !strcmp(a, "-h") ? 0 : 1;
        }
    }

    char pathbuf[128];
    if (out && OutOpen(Absolute(out, pathbuf, sizeof(pathbuf)), raw) != 0)
        printf("speedtest: continuing without output file\n");

    g_khz = MeasureKhz(100, 5);
    g_overhead = CalibrateOverhead();
    printf("speedtest: %u samples, %u warmup, clock %u.%03u MHz, timer overhead %u cyc\n",
           (unsigned)g_n, (unsigned)g_warmup, (unsigned)(g_khz / 1000), (unsigned)(g_khz % 1000),
           (unsigned)g_overhead);

    OutPrintf("# zuzu speedtest results v1\n");
    OutPrintf("# M,section,key,value\n");
    OutPrintf("# S,section,name,unit,n,min,p1,p5,p10,p25,p50,p75,p90,p95,p99,p999,max,mean,sd\n");
    OutPrintf("# R,section,name,unit,n,v0,v1,...\n");
    OutPrintf("# P,section,series,xlabel,ylabel,x,y\n");
    OutMeta("version", "1");
    OutMeta("clock_khz", "%u", (unsigned)g_khz);
    OutMeta("timer_overhead_cyc", "%u", (unsigned)g_overhead);
    OutMeta("samples", "%u", (unsigned)g_n);
    OutMeta("warmup", "%u", (unsigned)g_warmup);
    OutMeta("uptime_ms", "%llu", (unsigned long long)UptimeMs());
    OutMeta("raw", "%u", raw ? 1u : 0u);

    uint64_t t0 = UptimeUs();
    for (uint32_t k = 0; k < ARRAY_LEN(kSections); k++) {
        const Section *s = &kSections[k];
        if (only && !InList(only, s->name))
            continue;
        if (skip && InList(skip, s->name))
            continue;
        RunSection(s);
    }
    OutSection("end");
    OutMeta("total_ms", "%llu", (unsigned long long)((UptimeUs() - t0) / 1000));
    printf("speedtest: done in %llu ms\n", (unsigned long long)((UptimeUs() - t0) / 1000));
    OutClose();
    return 0;
}
