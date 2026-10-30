#include <stdio.h>
#include <string.h>

#include "speedtest.h"

#define MEM_MAX (8u * 1024u * 1024u)
#define MEM_MIN 2048u
#define BW_TARGET (8u * 1024u * 1024u)
#define BW_TRIALS 7u
#define LAT_LOADS (256u * 1024u)
#define LINE 64u

static uint8_t *g_a;
static uint8_t *g_b;
static uint32_t g_cap;

static void ReadBlock(const void *p, uint32_t bytes)
{
    __asm__ volatile("1:\n"
                     "ldmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
                     "ldmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
                     "subs %1, %1, #64\n"
                     "bgt 1b\n"
                     : "+r"(p), "+r"(bytes)
                     :
                     : "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "cc", "memory");
}

static void WriteBlock(void *p, uint32_t bytes)
{
    __asm__ volatile("mov r2, #0\n"
                     "mov r3, #0\n"
                     "mov r4, #0\n"
                     "mov r5, #0\n"
                     "mov r6, #0\n"
                     "mov r8, #0\n"
                     "mov r9, #0\n"
                     "mov r10, #0\n"
                     "1:\n"
                     "stmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
                     "stmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
                     "subs %1, %1, #64\n"
                     "bgt 1b\n"
                     : "+r"(p), "+r"(bytes)
                     :
                     : "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "cc", "memory");
}

static bool MemSetup(void)
{
    if (g_a)
        return true;
    for (g_cap = MEM_MAX; g_cap >= 256u * 1024u; g_cap /= 2) {
        void *a = MemMapAnon(g_cap, 0, PROT_RW);
        if (PtrIsErr(a))
            continue;
        void *b = MemMapAnon(g_cap, 0, PROT_RW);
        if (PtrIsErr(b)) {
            MemUnmap(a);
            continue;
        }
        g_a = a;
        g_b = b;
        for (uint32_t o = 0; o < g_cap; o += PAGE) {
            g_a[o] = 1;
            g_b[o] = 2;
        }
        OutMeta("mem_buffer_bytes", "%u", (unsigned)g_cap);
        return true;
    }
    printf("mem bench: no memory\n");
    return false;
}

static void MemTeardown(void)
{
    if (!g_a)
        return;
    MemUnmap(g_a);
    MemUnmap(g_b);
    g_a = g_b = NULL;
}

static uint32_t NextSize(uint32_t s)
{
    uint32_t pow = 1;
    while (pow * 2 <= s)
        pow *= 2;
    return s == pow ? s + s / 2 : pow * 2;
}

typedef enum {
    BW_READ,
    BW_WRITE,
    BW_COPY,
    BW_MEMSET,
} BwKind;

static const char *const kBwName[] = {"read (ldm)", "write (stm)", "copy (memcpy)",
                                      "memset"};

static uint32_t BwRun(BwKind kind, uint32_t size, uint32_t reps)
{
    uint32_t t0 = ArchMeasure();
    for (uint32_t r = 0; r < reps; r++) {
        switch (kind) {
        case BW_READ:
            ReadBlock(g_a, size);
            break;
        case BW_WRITE:
            WriteBlock(g_a, size);
            break;
        case BW_COPY:
            memcpy(g_b, g_a, size);
            break;
        case BW_MEMSET:
            memset(g_a, (int)r, size);
            break;
        }
    }
    return ArchMeasure() - t0;
}

void BenchMemBandwidth(void)
{
    if (!MemSetup())
        return;
    printf("%-14s %10s %10s %10s\n", "bandwidth", "bytes", "best MB/s", "med MB/s");
    for (uint32_t kind = 0; kind < ARRAY_LEN(kBwName); kind++) {
        char series[40];
        snprintf(series, sizeof(series), "bw %s", kBwName[kind]);
        for (uint32_t size = MEM_MIN; size <= g_cap; size = NextSize(size)) {
            uint32_t reps = BW_TARGET / size;
            if (reps == 0)
                reps = 1;
            (void)BwRun((BwKind)kind, size, reps > 4 ? 4 : reps);
            uint32_t t[BW_TRIALS];
            for (uint32_t i = 0; i < BW_TRIALS; i++)
                t[i] = BwRun((BwKind)kind, size, reps);
            SortU32(t, BW_TRIALS);
            uint64_t bytes = (uint64_t)size * reps;
            uint64_t best_ns = CyclesToNs64(t[0]);
            uint64_t med_ns = CyclesToNs64(t[BW_TRIALS / 2]);
            uint64_t best = best_ns ? bytes * 1000000u / best_ns : 0;
            uint64_t med = med_ns ? bytes * 1000000u / med_ns : 0;
            printf("%-14s %10u %6llu.%03u %6llu.%03u\n", kBwName[kind], (unsigned)size,
                   (unsigned long long)(best / 1000), (unsigned)(best % 1000),
                   (unsigned long long)(med / 1000), (unsigned)(med % 1000));
            Point(series, "bytes", size, "MB/s", best);
        }
    }
}

static uint32_t g_rng = 0x9E3779B9u;

static uint32_t Rand(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void BuildChain(uint32_t size, bool random)
{
    uint32_t lines = size / LINE;
    uint32_t *idx = (uint32_t *)g_b;
    for (uint32_t i = 0; i < lines; i++)
        idx[i] = i;
    if (random) {
        for (uint32_t i = lines - 1; i > 0; i--) {
            uint32_t j = Rand() % i;
            uint32_t t = idx[i];
            idx[i] = idx[j];
            idx[j] = t;
        }
    }
    for (uint32_t i = 0; i < lines; i++) {
        uint32_t from = idx[i];
        uint32_t to = idx[(i + 1) % lines];
        *(void **)(g_a + from * LINE) = g_a + to * LINE;
    }
}

static __attribute__((noinline)) void *Chase(void *p, uint32_t loads)
{
    void **q = p;
    for (uint32_t i = 0; i < loads; i += 8) {
        q = *q;
        q = *q;
        q = *q;
        q = *q;
        q = *q;
        q = *q;
        q = *q;
        q = *q;
    }
    return q;
}

void BenchMemLatency(void)
{
    if (!MemSetup())
        return;
    printf("%-16s %10s %10s %10s\n", "latency", "bytes", "cyc/load", "ns/load");
    volatile uintptr_t sink = 0;
    for (int mode = 0; mode < 2; mode++) {
        const char *series = mode ? "load latency random" : "load latency sequential";
        for (uint32_t size = MEM_MIN; size <= g_cap; size = NextSize(size)) {
            BuildChain(size, mode != 0);
            sink += (uintptr_t)Chase(g_a, LAT_LOADS / 4);
            uint32_t t[5];
            for (uint32_t i = 0; i < ARRAY_LEN(t); i++) {
                uint32_t t0 = ArchMeasure();
                sink += (uintptr_t)Chase(g_a, LAT_LOADS);
                t[i] = ArchMeasure() - t0;
            }
            SortU32(t, ARRAY_LEN(t));
            uint64_t cyc_milli = (uint64_t)t[ARRAY_LEN(t) / 2] * 1000u / LAT_LOADS;
            uint64_t ns_milli = CyclesToNs64(cyc_milli);
            printf("%-16s %10u %6llu.%03u %6llu.%03u\n", mode ? "random" : "sequential",
                   (unsigned)size, (unsigned long long)(cyc_milli / 1000),
                   (unsigned)(cyc_milli % 1000), (unsigned long long)(ns_milli / 1000),
                   (unsigned)(ns_milli % 1000));
            Point(series, "bytes", size, "ns", ns_milli);
        }
    }
    (void)sink;
    MemTeardown();
}
