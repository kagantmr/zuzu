#include <stdio.h>
#include <string.h>
#include <zuzu/uptime.h>

#include "speedtest.h"

#define FS_PATH "/speedtest.tmp"
#define FS_TRIALS 5u
#define FS_META_N 200u

void BenchNoise(void)
{
    uint32_t thresh = g_overhead * 2 + 300;
    uint64_t budget = (uint64_t)g_noise_ms * g_khz;
    uint64_t elapsed = 0;
    uint64_t stolen = 0;
    uint32_t events = 0;
    uint32_t stored = 0;
    uint32_t max_gap = 0;

    uint32_t start = ArchMeasure();
    uint32_t prev = start;
    while (elapsed < budget) {
        uint32_t now = ArchMeasure();
        uint32_t d = now - prev;
        elapsed += d;
        if (d > thresh) {
            events++;
            stolen += d;
            if (d > max_gap)
                max_gap = d;
            if (stored < SAMPLES) {
                g_samples_c[stored] = (uint32_t)(CyclesToNs64(elapsed - d) / 1000u);
                g_samples_d[stored] = d;
                stored++;
            }
        }
        prev = now;
    }

    uint64_t pct_milli = elapsed ? stolen * 100000u / elapsed : 0;
    printf("noise: %u ms spin, threshold %u cyc, %u interruptions, %llu.%03u%% stolen, worst %u ns\n",
           (unsigned)g_noise_ms, (unsigned)thresh, (unsigned)events,
           (unsigned long long)(pct_milli / 1000), (unsigned)(pct_milli % 1000),
           CyclesToNs(max_gap));
    OutMeta("noise_ms", "%u", (unsigned)g_noise_ms);
    OutMeta("noise_threshold_cyc", "%u", (unsigned)thresh);
    OutMeta("noise_events", "%u", (unsigned)events);
    OutMeta("noise_stolen_pct", "%llu.%03u", (unsigned long long)(pct_milli / 1000),
            (unsigned)(pct_milli % 1000));
    OutMeta("noise_worst_ns", "%u", (unsigned)CyclesToNs(max_gap));
    if (events && g_noise_ms)
        OutMeta("noise_rate_hz", "%u", (unsigned)((uint64_t)events * 1000u / g_noise_ms));

    for (uint32_t i = 0; i < stored; i++)
        Point("noise timeline", "us", g_samples_c[i], "ns",
              CyclesToNs64((uint64_t)g_samples_d[i] * 1000));
    if (stored > 1) {
        for (uint32_t i = 1; i < stored; i++)
            g_samples_b[i - 1] = g_samples_c[i] - g_samples_c[i - 1];
        ReportUnit("noise inter-arrival", g_samples_b, stored - 1, UNIT_US, false);
    }
    if (stored)
        ReportUnit("noise gap", g_samples_d, stored, UNIT_CYC, false);
}

static Err FsWriteFile(FsdConn *c, uint32_t bytes, uint32_t chunk, const uint8_t *src)
{
    uint32_t fd;
    Err rc = FsdOpen(c, FS_PATH, FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS, &fd);
    if (rc != ZUZU_OK)
        return rc;
    uint32_t done = 0;
    while (done < bytes && rc == ZUZU_OK) {
        uint32_t want = bytes - done < chunk ? bytes - done : chunk;
        uint32_t put = 0;
        rc = FsdWrite(c, fd, src, want, &put);
        if (rc == ZUZU_OK && put == 0)
            rc = ERR_IO;
        done += put;
    }
    Err rc2 = FsdClose(c, fd);
    return rc != ZUZU_OK ? rc : rc2;
}

static Err FsReadFile(FsdConn *c, uint32_t bytes, uint32_t chunk, uint8_t *dst)
{
    uint32_t fd;
    Err rc = FsdOpen(c, FS_PATH, FSD_MODE_READ, &fd);
    if (rc != ZUZU_OK)
        return rc;
    uint32_t done = 0;
    while (done < bytes && rc == ZUZU_OK) {
        uint32_t want = bytes - done < chunk ? bytes - done : chunk;
        uint32_t got = 0;
        rc = FsdRead(c, fd, dst, want, &got);
        if (rc == ZUZU_OK && got == 0)
            rc = ERR_IO;
        done += got;
    }
    Err rc2 = FsdClose(c, fd);
    return rc != ZUZU_OK ? rc : rc2;
}

static uint64_t Throughput(uint32_t bytes, uint32_t us) { return us ? (uint64_t)bytes * 1000u / us : 0; }

static bool FsSweep(FsdConn *c, const char *what, uint32_t bytes, uint32_t chunk, uint8_t *buf,
                    bool write, uint32_t x, const char *xlabel)
{
    for (uint32_t i = 0; i < FS_TRIALS; i++) {
        uint64_t t0 = UptimeUs();
        Err rc = write ? FsWriteFile(c, bytes, chunk, buf) : FsReadFile(c, bytes, chunk, buf);
        g_samples_b[i] = (uint32_t)(UptimeUs() - t0);
        if (rc != ZUZU_OK) {
            printf("fs %s %u bytes failed: %d\n", write ? "write" : "read", (unsigned)bytes,
                   (int)rc);
            return false;
        }
    }
    char name[64];
    snprintf(name, sizeof(name), "fs %s %uK chunk %u", write ? "write" : "read",
             (unsigned)(bytes / 1024), (unsigned)chunk);
    ReportUnit(name, g_samples_b, FS_TRIALS, UNIT_US, false);
    uint32_t med = g_samples[FS_TRIALS / 2];
    Point(what, xlabel, x, "MB/s", Throughput(bytes, med));
    return true;
}

void BenchFs(void)
{
    FsdConn *c = FsConn();
    if (!c) {
        printf("fs bench: fsd unavailable, skipped\n");
        return;
    }
    uint8_t *buf = MemMapAnon(256u * 1024u, 0, PROT_RW);
    if (PtrIsErr(buf))
        return;
    for (uint32_t i = 0; i < 256u * 1024u; i++)
        buf[i] = (uint8_t)(i * 31u + 7u);
    uint32_t chunk = c->size - FSD_DATA_OFF;
    OutMeta("fsd_shm_bytes", "%u", (unsigned)chunk);

    static const uint32_t sizes[] = {4096, 16384, 65536, 262144, 1048576};
    for (uint32_t k = 0; k < ARRAY_LEN(sizes); k++)
        if (!FsSweep(c, "fs write vs size", sizes[k], chunk, buf, true, sizes[k], "bytes"))
            goto out;
    for (uint32_t k = 0; k < ARRAY_LEN(sizes); k++) {
        if (FsWriteFile(c, sizes[k], chunk, buf) != ZUZU_OK)
            goto out;
        if (!FsSweep(c, "fs read vs size", sizes[k], chunk, buf, false, sizes[k], "bytes"))
            goto out;
    }

    static const uint32_t chunks[] = {512, 2048, 8192, 32768, 65536};
    for (uint32_t k = 0; k < ARRAY_LEN(chunks); k++) {
        if (chunks[k] > c->size)
            break;
        if (!FsSweep(c, "fs write 256K vs chunk", 262144, chunks[k], buf, true, chunks[k],
                     "chunk bytes"))
            goto out;
    }
    for (uint32_t k = 0; k < ARRAY_LEN(chunks); k++) {
        if (chunks[k] > c->size)
            break;
        if (!FsSweep(c, "fs read 256K vs chunk", 262144, chunks[k], buf, false, chunks[k],
                     "chunk bytes"))
            goto out;
    }

    FsdStat st;
    for (uint32_t i = 0; i < FS_META_N; i++)
        TIMED(g_samples, i, FsdGetStat(c, FS_PATH, &st));
    Report("fs stat", FS_META_N);
    uint32_t got = 0;
    for (uint32_t i = 0; i < FS_META_N; i++) {
        uint32_t fd;
        Err rc;
        TIMED(g_samples, i, rc = FsdOpen(c, FS_PATH, FSD_MODE_READ, &fd));
        if (rc != ZUZU_OK)
            break;
        TIMED(g_samples_b, i, FsdClose(c, fd));
        got++;
    }
    Report("fs open (read)", got);
    ReportBuf("fs close (read)", g_samples_b, got);

    for (uint32_t i = 0; i < 50; i++) {
        uint32_t fd;
        uint64_t t0 = UptimeUs();
        if (FsdOpen(c, FS_PATH, FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS, &fd) != ZUZU_OK)
            break;
        FsdClose(c, fd);
        uint64_t t1 = UptimeUs();
        FsdUnlink(c, FS_PATH);
        g_samples_b[i] = (uint32_t)(t1 - t0);
        g_samples_c[i] = (uint32_t)(UptimeUs() - t1);
        got = i + 1;
    }
    ReportUnit("fs create+close", g_samples_b, got, UNIT_US, false);
    ReportUnit("fs unlink", g_samples_c, got, UNIT_US, false);

out:
    FsdUnlink(c, FS_PATH);
    MemUnmap(buf);
}
