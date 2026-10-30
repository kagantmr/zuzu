#include <stdio.h>
#include <string.h>
#include <zuzu/uptime.h>

#include "speedtest.h"

uint32_t g_samples[SAMPLES];
uint32_t g_samples_b[SAMPLES];
uint32_t g_samples_c[SAMPLES];
uint32_t g_samples_d[SAMPLES];
uint32_t g_overhead;
uint32_t g_khz;
uint32_t g_n = SAMPLES;
uint32_t g_warmup = WARMUP;
uint32_t g_noise_ms = 2000;

static const uint16_t kPermille[] = {0, 10, 50, 100, 250, 500, 750, 900, 950, 990, 999, 1000};

void SortU32(uint32_t *a, uint32_t n)
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

uint32_t CyclesToNs(uint32_t cycles) { return (uint32_t)CyclesToNs64(cycles); }

uint64_t CyclesToNs64(uint64_t cycles) { return (cycles * 1000000u) / g_khz; }

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

uint32_t MeasureKhz(uint32_t span_ms, uint32_t rounds)
{
    uint32_t est[9];
    if (rounds > ARRAY_LEN(est))
        rounds = ARRAY_LEN(est);
    for (uint32_t r = 0; r < rounds; r++) {
        uint64_t t1 = UptimeUs();
        uint32_t c0 = ArchMeasure();
        while (UptimeUs() - t1 < (uint64_t)span_ms * 1000u)
            ;
        uint32_t c1 = ArchMeasure();
        uint64_t t2 = UptimeUs();
        est[r] = (uint32_t)(((uint64_t)(c1 - c0) * 1000u) / (t2 - t1));
    }
    SortU32(est, rounds);
    return est[rounds / 2];
}

uint32_t CalibrateOverhead(void)
{
    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t a = ArchMeasure();
        uint32_t b = ArchMeasure();
        g_samples[i] = b - a;
    }
    SortU32(g_samples, SAMPLES);
    return g_samples[SAMPLES / 2];
}

static uint32_t Pct(const uint32_t *sorted, uint32_t n, uint32_t permille)
{
    return sorted[(uint32_t)(((uint64_t)(n - 1) * permille + 500) / 1000)];
}

void ReportUnit(const char *name, const uint32_t *buf, uint32_t n, Unit unit, bool sub_overhead)
{
    if (n == 0)
        return;
    if (buf != g_samples)
        memcpy(g_samples, buf, n * sizeof(uint32_t));
    if (sub_overhead) {
        for (uint32_t i = 0; i < n; i++)
            g_samples[i] = g_samples[i] > g_overhead ? g_samples[i] - g_overhead : 0;
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++)
        sum += g_samples[i];
    uint32_t mean = (uint32_t)(sum / n);

    uint64_t var = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t d = (int64_t)g_samples[i] - mean;
        var += (uint64_t)(d * d);
    }
    uint32_t sd = ISqrt64(var / n);

    const char *u = unit == UNIT_CYC ? "cyc" : "us";
    if (OutEnabled() && OutRaw()) {
        OutPrintf("R,%s,%s,%s,%u", OutSectionName(), name, u, (unsigned)n);
        for (uint32_t i = 0; i < n; i++)
            OutPrintf(",%u", (unsigned)g_samples[i]);
        OutPrintf("\n");
    }

    SortU32(g_samples, n);
    uint32_t pct[ARRAY_LEN(kPermille)];
    for (uint32_t k = 0; k < ARRAY_LEN(kPermille); k++)
        pct[k] = Pct(g_samples, n, kPermille[k]);

    uint32_t min = pct[0], med = pct[5], p99 = pct[9], max = pct[11];
    if (unit == UNIT_CYC) {
        printf("%-30s min %6u  med %6u  p99 %7u  max %8u  mean %6u  sd %6u cyc | med %7u ns\n",
               name, min, med, p99, max, mean, sd, CyclesToNs(med));
    } else {
        printf("%-30s min %6u  med %6u  p99 %7u  max %8u  mean %6u  sd %6u us\n", name, min, med,
               p99, max, mean, sd);
    }

    if (OutEnabled()) {
        OutPrintf("S,%s,%s,%s,%u", OutSectionName(), name, u, (unsigned)n);
        for (uint32_t k = 0; k < ARRAY_LEN(kPermille); k++)
            OutPrintf(",%u", (unsigned)pct[k]);
        OutPrintf(",%u,%u\n", (unsigned)mean, (unsigned)sd);
    }
}

void Report(const char *name, uint32_t n) { ReportUnit(name, g_samples, n, UNIT_CYC, true); }

void ReportBuf(const char *name, const uint32_t *buf, uint32_t n)
{
    ReportUnit(name, buf, n, UNIT_CYC, true);
}

void Point(const char *series, const char *xlabel, uint32_t x, const char *ylabel,
           uint64_t y_milli)
{
    if (!OutEnabled())
        return;
    OutPrintf("P,%s,%s,%s,%s,%u,%llu.%03u\n", OutSectionName(), series, xlabel, ylabel, (unsigned)x,
              (unsigned long long)(y_milli / 1000), (unsigned)(y_milli % 1000));
}
