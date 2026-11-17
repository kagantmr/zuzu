#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/uptime.h>

#include "cyclictest.h"

#define MAX_SAMPLES 120000u
#define WARMUP 50u
#define SETTLE_MS 200u
#define HIST_NS 100u
#define HIST_BUCKETS 10000u
#define HIST_PLOT_NS 1000u
#define PROGRESS_SAMPLES 600000u
#define TIMELINE_CHUNK 1000u

volatile uint32_t g_stop;
uint32_t g_khz;

static uint32_t g_interval_ms = 1;
static uint32_t g_duration_s = 10;
static uint32_t g_per_kind = 2;
static uint32_t g_prio = 7;
static uint32_t g_load_prio = 1;
static bool g_raw;

static uint32_t *g_lat;
static uint32_t *g_sorted;
static uint32_t *g_chunk_max;
static uint32_t g_cap;
static uint32_t g_hist[HIST_BUCKETS + 1];
static uint64_t g_sum;
static uint64_t g_sumsq;
static uint32_t g_min;
static uint32_t g_max;
static uint32_t g_over;
static Worker g_workers[MAX_WORKERS];

static const uint16_t kPermille[] = {0, 10, 50, 100, 250, 500, 750, 900, 950, 990, 999, 1000};

typedef struct {
    const char *name;
    uint32_t n;
    uint32_t pct[ARRAY_LEN(kPermille)];
    uint32_t mean;
    uint32_t sd;
    uint32_t over;
    uint64_t ops;
    uint32_t workers;
} Result;

static Result g_results[16];
static uint32_t g_nresults;

static void SortU32(uint32_t *a, uint32_t n)
{
    for (uint32_t gap = n / 2; gap > 0; gap /= 2) {
        for (uint32_t i = gap; i < n; i++) {
            uint32_t v = a[i];
            uint32_t j = i;
            for (; j >= gap && a[j - gap] > v; j -= gap)
                a[j] = a[j - gap];
            a[j] = v;
        }
    }
}

static uint32_t ISqrt64(uint64_t v)
{
    uint64_t r = 0;
    uint64_t bit = 1ull << 62;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

static uint32_t MeasureKhz(uint32_t span_ms)
{
    uint64_t t1 = UptimeUs();
    uint32_t c0 = ArchMeasure();
    while (UptimeUs() - t1 < (uint64_t)span_ms * 1000u)
        ;
    uint32_t c1 = ArchMeasure();
    uint64_t t2 = UptimeUs();
    return (uint32_t)(((uint64_t)(c1 - c0) * 1000u) / (t2 - t1));
}

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

static void Point(const char *series, const char *xlabel, uint32_t x, const char *ylabel,
                  uint64_t y_milli)
{
    OutPrintf("P,%s,%s,%s,%s,%u,%llu.%03u\n", OutSectionName(), series, xlabel, ylabel, (unsigned)x,
              (unsigned long long)(y_milli / 1000), (unsigned)(y_milli % 1000));
}

static void Us(char *buf, size_t n, uint32_t ns)
{
    snprintf(buf, n, "%u.%u", (unsigned)(ns / 1000), (unsigned)(ns % 1000 / 100));
}

static void SummaryLine(const Result *r)
{
    char mn[16], med[16], p99[16], p999[16], mx[16], avg[16];
    Us(mn, sizeof(mn), r->pct[0]);
    Us(med, sizeof(med), r->pct[5]);
    Us(p99, sizeof(p99), r->pct[9]);
    Us(p999, sizeof(p999), r->pct[10]);
    Us(mx, sizeof(mx), r->pct[11]);
    Us(avg, sizeof(avg), r->mean);
    printf("%-8s n %6u  min %6s  avg %6s  med %6s  p99 %6s  p99.9 %7s  max %8s us", r->name,
           (unsigned)r->n, mn, avg, med, p99, p999, mx);
    if (r->over)
        printf("  overruns %u", (unsigned)r->over);
    printf("\n");
}

static void WriteStats(const char *name, const Result *r)
{
    OutPrintf("S,%s,%s,ns,%u", OutSectionName(), name, (unsigned)r->n);
    for (uint32_t k = 0; k < ARRAY_LEN(kPermille); k++)
        OutPrintf(",%u", (unsigned)r->pct[k]);
    OutPrintf(",%u,%u\n", (unsigned)r->mean, (unsigned)r->sd);
}

static uint32_t HistPct(uint32_t n, uint32_t permille)
{
    uint64_t rank = ((uint64_t)(n - 1) * permille + 500) / 1000;
    uint64_t seen = 0;
    for (uint32_t b = 0; b < HIST_BUCKETS; b++) {
        seen += g_hist[b];
        if (seen > rank)
            return (b + 1) * HIST_NS - 1;
    }
    return g_max;
}

static void Analyze(Result *r, uint32_t n)
{
    r->n = n;
    r->over = g_over;
    r->mean = (uint32_t)(g_sum / n);
    uint64_t m2 = g_sumsq / n;
    uint64_t mm = (uint64_t)r->mean * r->mean;
    r->sd = ISqrt64(m2 > mm ? m2 - mm : 0);
    if (n <= g_cap) {
        memcpy(g_sorted, g_lat, n * sizeof(uint32_t));
        SortU32(g_sorted, n);
        for (uint32_t k = 0; k < ARRAY_LEN(kPermille); k++)
            r->pct[k] = g_sorted[(uint32_t)(((uint64_t)(n - 1) * kPermille[k] + 500) / 1000)];
    } else {
        for (uint32_t k = 0; k < ARRAY_LEN(kPermille); k++)
            r->pct[k] = HistPct(n, kPermille[k]);
    }
    r->pct[0] = g_min;
    r->pct[ARRAY_LEN(kPermille) - 1] = g_max;
}

static void WriteDetail(const Result *r)
{
    if (!OutEnabled())
        return;
    WriteStats("wake latency", r);
    if (g_raw && r->n <= g_cap) {
        OutPrintf("R,%s,wake latency,ns,%u", OutSectionName(), (unsigned)r->n);
        for (uint32_t i = 0; i < r->n; i++)
            OutPrintf(",%u", (unsigned)g_lat[i]);
        OutPrintf("\n");
    }
    const uint32_t per = HIST_PLOT_NS / HIST_NS;
    for (uint32_t b = 0; b <= HIST_BUCKETS; b += per) {
        uint64_t c = 0;
        for (uint32_t j = b; j < b + per && j <= HIST_BUCKETS; j++)
            c += g_hist[j];
        if (c)
            Point("latency histogram", "us", b / per, "samples", c * 1000u);
    }
    for (uint32_t i = 0; i * TIMELINE_CHUNK < r->n; i++)
        Point("worst per 1000 samples", "sample", i * TIMELINE_CHUNK, "us", g_chunk_max[i]);
}

static void Progress(uint32_t done, uint32_t n, uint64_t t0_us)
{
    char mx[16], avg[16];
    Us(mx, sizeof(mx), g_max);
    Us(avg, sizeof(avg), (uint32_t)(g_sum / done));
    printf("  %llu min: %u/%u samples, avg %s us, max %s us, overruns %u\n",
           (unsigned long long)((UptimeUs() - t0_us) / 60000000u), (unsigned)done, (unsigned)n,
           avg, mx, (unsigned)g_over);
}

static uint32_t Measure(uint32_t n)
{
    uint64_t want = (uint64_t)g_interval_ms *
                    ((const volatile Syspage *)SYSPAGE)->counter_freq / 1000u;
    uint64_t interval_ns = (uint64_t)g_interval_ms * 1000000u;
    memset(g_hist, 0, sizeof(g_hist));
    g_sum = g_sumsq = 0;
    g_min = UINT32_MAX;
    g_max = 0;
    g_over = 0;
    for (uint32_t i = 0; i < WARMUP; i++)
        Sleep(g_interval_ms);
    uint64_t t0_us = UptimeUs();
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t0 = UptimeCounts();
        Sleep(g_interval_ms);
        uint64_t d = UptimeCounts() - t0;
        uint32_t v = (uint32_t)UptimeCountsTo(d > want ? d - want : 0, 1000000000u);
        if (i < g_cap)
            g_lat[i] = v;
        g_sum += v;
        g_sumsq += (uint64_t)v * v;
        if (v < g_min)
            g_min = v;
        if (v > g_max)
            g_max = v;
        if (v >= interval_ns)
            g_over++;
        uint32_t b = v / HIST_NS;
        g_hist[b < HIST_BUCKETS ? b : HIST_BUCKETS]++;
        uint32_t c = i / TIMELINE_CHUNK;
        if (i % TIMELINE_CHUNK == 0 || v > g_chunk_max[c])
            g_chunk_max[c] = v;
        if (n > MAX_SAMPLES && (i + 1) % PROGRESS_SAMPLES == 0)
            Progress(i + 1, n, t0_us);
    }
    return n;
}

static void RunLoad(const Load *l, uint32_t n)
{
    OutSection(l->name);
    printf("== %s: %s\n", l->name, l->desc);
    uint32_t khz0 = MeasureKhz(20);
    uint32_t nw = LoadStart(l, g_per_kind, g_load_prio, g_workers, MAX_WORKERS);
    Sleep(SETTLE_MS);
    uint64_t t0 = UptimeUs();
    Measure(n);
    uint64_t dt = UptimeUs() - t0;
    uint64_t ops = 0;
    for (uint32_t i = 0; i < nw; i++)
        ops += g_workers[i].ops;
    LoadStop(g_workers, nw);
    uint32_t khz1 = MeasureKhz(20);

    for (uint32_t i = 0; i < nw; i++) {
        Worker *w = &g_workers[i];
        if (w->done && w->err != ZUZU_OK)
            printf("cyclictest: %s worker failed: %d\n", w->kind, (int)w->err);
    }

    Result *r = g_nresults < ARRAY_LEN(g_results) ? &g_results[g_nresults++] : &g_results[0];
    r->name = l->name;
    r->ops = ops;
    r->workers = nw;
    Analyze(r, n);
    SummaryLine(r);
    if (nw)
        printf("         %u workers, %llu ops in %llu ms\n", (unsigned)nw,
               (unsigned long long)ops, (unsigned long long)(dt / 1000));
    uint32_t d0 = khz0 > g_khz ? khz0 - g_khz : g_khz - khz0;
    uint32_t d1 = khz1 > g_khz ? khz1 - g_khz : g_khz - khz1;
    if ((uint64_t)(d0 > d1 ? d0 : d1) * 200 > g_khz)
        printf("!! clock drift in %s: %u -> %u kHz (calibrated %u)\n", l->name, (unsigned)khz0,
               (unsigned)khz1, (unsigned)g_khz);

    OutMeta("clock_khz_before", "%u", (unsigned)khz0);
    OutMeta("clock_khz_after", "%u", (unsigned)khz1);
    OutMeta("duration_ms", "%llu", (unsigned long long)(dt / 1000));
    OutMeta("workers", "%u", (unsigned)nw);
    OutMeta("ops", "%llu", (unsigned long long)ops);
    OutMeta("overruns", "%u", (unsigned)r->over);
    WriteDetail(r);
    OutFlush();
}

static void Usage(void)
{
    printf("usage: cyclictest [-o FILE] [-r] [-i MS] [-d SEC] [-l LIST] [-w N] [-p PRIO] [-L]\n"
           "  -o FILE  save results as CSV on the SD card (graph with scripts/speedtest_plot.py)\n"
           "  -r       also save every raw sample (runs up to %u samples)\n"
           "  -i MS    wakeup interval (1..100, default 1)\n"
           "  -d SEC   seconds measured per load (default 10)\n"
           "  -l LIST  run only these loads (comma-separated, default all)\n"
           "  -w N     workers per load kind (1..%u, default 2)\n"
           "  -p PRIO  priority of the measuring task (default 7, loads run at 1)\n"
           "  -L       list loads\n",
           (unsigned)MAX_SAMPLES, (unsigned)MAX_PER_KIND);
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

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        long x = v ? strtol(v, NULL, 10) : 0;
        if (!strcmp(a, "-o") && v) {
            out = v;
            i++;
        } else if (!strcmp(a, "-r")) {
            g_raw = true;
        } else if (!strcmp(a, "-i") && v) {
            g_interval_ms = x < 1 ? 1 : x > 100 ? 100 : (uint32_t)x;
            i++;
        } else if (!strcmp(a, "-d") && v) {
            g_duration_s = x < 1 ? 1 : x > 172800 ? 172800 : (uint32_t)x;
            i++;
        } else if (!strcmp(a, "-l") && v) {
            only = v;
            i++;
        } else if (!strcmp(a, "-w") && v) {
            g_per_kind = x < 1 ? 1 : x > MAX_PER_KIND ? MAX_PER_KIND : (uint32_t)x;
            i++;
        } else if (!strcmp(a, "-p") && v) {
            g_prio = x < 2 ? 2 : (uint32_t)x;
            i++;
        } else if (!strcmp(a, "-L")) {
            for (uint32_t k = 0; k < LoadCount(); k++)
                printf("  %-8s %s\n", LoadAt(k)->name, LoadAt(k)->desc);
            return 0;
        } else {
            Usage();
            return !strcmp(a, "-h") ? 0 : 1;
        }
    }
    if (only) {
        for (const char *p = only; *p;) {
            const char *e = strchr(p, ',');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            char name[16];
            snprintf(name, sizeof(name), "%.*s", (int)len, p);
            if (!LoadFind(name)) {
                printf("cyclictest: unknown load '%s' (-L lists them)\n", name);
                return 1;
            }
            if (!e)
                break;
            p = e + 1;
        }
    }

    uint32_t n = (uint32_t)((uint64_t)g_duration_s * 1000u / g_interval_ms);
    if (n < 16)
        n = 16;
    g_cap = n < MAX_SAMPLES ? n : MAX_SAMPLES;
    size_t bytes = ((size_t)g_cap * sizeof(uint32_t) + PAGE - 1) & ~(size_t)(PAGE - 1);
    uint32_t nchunks = (n + TIMELINE_CHUNK - 1) / TIMELINE_CHUNK;
    size_t cbytes = ((size_t)nchunks * sizeof(uint32_t) + PAGE - 1) & ~(size_t)(PAGE - 1);
    g_lat = MemMapAnon(bytes, 0, PROT_RW);
    g_sorted = MemMapAnon(bytes, 0, PROT_RW);
    g_chunk_max = MemMapAnon(cbytes, 0, PROT_RW);
    if (PtrIsErr(g_lat) || PtrIsErr(g_sorted) || PtrIsErr(g_chunk_max)) {
        printf("cyclictest: no memory for %u samples\n", (unsigned)n);
        return 1;
    }
    memset(g_lat, 0, bytes);
    memset(g_sorted, 0, bytes);
    memset(g_chunk_max, 0, cbytes);

    char pathbuf[128];
    if (out && OutOpen(Absolute(out, pathbuf, sizeof(pathbuf))) != 0)
        printf("cyclictest: continuing without output file\n");

    Err pe = TaskSetPriority(-1, g_prio);
    if (pe != ZUZU_OK) {
        printf("cyclictest: cannot run at priority %u: %d\n", (unsigned)g_prio, (int)pe);
        return 1;
    }

    g_khz = MeasureKhz(100);
    printf("cyclictest: interval %u ms, %u samples/load, prio %u over loads at %u, %u workers/kind, "
           "clock %u.%03u MHz\n",
           (unsigned)g_interval_ms, (unsigned)n, (unsigned)g_prio, (unsigned)g_load_prio,
           (unsigned)g_per_kind, (unsigned)(g_khz / 1000), (unsigned)(g_khz % 1000));

    OutPrintf("# zuzu cyclictest results v1 (speedtest v1 format)\n");
    OutPrintf("# M,section,key,value\n");
    OutPrintf("# S,section,name,unit,n,min,p1,p5,p10,p25,p50,p75,p90,p95,p99,p999,max,mean,sd\n");
    OutPrintf("# R,section,name,unit,n,v0,v1,...\n");
    OutPrintf("# P,section,series,xlabel,ylabel,x,y\n");
    OutMeta("tool", "cyclictest");
    OutMeta("version", "1");
    OutMeta("clock_khz", "%u", (unsigned)g_khz);
    OutMeta("samples", "%u", (unsigned)n);
    OutMeta("interval_ms", "%u", (unsigned)g_interval_ms);
    OutMeta("prio", "%u", (unsigned)g_prio);
    OutMeta("load_prio", "%u", (unsigned)g_load_prio);
    OutMeta("workers_per_kind", "%u", (unsigned)g_per_kind);
    OutMeta("counter_hz", "%llu",
            (unsigned long long)((const volatile Syspage *)SYSPAGE)->counter_freq);
    OutMeta("uptime_ms", "%llu", (unsigned long long)UptimeMs());
    OutMeta("raw", "%u", g_raw ? 1u : 0u);

    uint64_t t0 = UptimeUs();
    for (uint32_t k = 0; k < LoadCount(); k++) {
        const Load *l = LoadAt(k);
        if (only && !InList(only, l->name))
            continue;
        RunLoad(l, n);
    }

    if (g_nresults > 1) {
        printf("== summary (wake latency past the deadline)\n");
        OutSection("summary");
        for (uint32_t i = 0; i < g_nresults; i++) {
            SummaryLine(&g_results[i]);
            if (OutEnabled())
                WriteStats(g_results[i].name, &g_results[i]);
        }
    }
    OutSection("end");
    OutMeta("total_ms", "%llu", (unsigned long long)((UptimeUs() - t0) / 1000));
    printf("cyclictest: done in %llu ms\n", (unsigned long long)((UptimeUs() - t0) / 1000));
    OutClose();
    return 0;
}
