#ifndef SPEEDTEST_H
#define SPEEDTEST_H

#include <arch/cycles.h>
#include <fs/fsd_client.h>
#include <stdbool.h>
#include <stdint.h>
#include <zuzu/zuzu.h>

#define SAMPLES 8192
#define WARMUP 1024
#define SERVER_STACK (16 * 1024)
#define MSG_QUIT 0xFFFFFFFFu
#define HEAVY 1100
#define PAGE 4096u

#define TIMED(buf, i, expr)                                                                        \
    do {                                                                                           \
        uint32_t t0_ = ArchMeasure();                                                              \
        expr;                                                                                      \
        (buf)[i] = ArchMeasure() - t0_;                                                            \
    } while (0)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
    UNIT_CYC,
    UNIT_US,
} Unit;

extern uint32_t g_samples[SAMPLES];
extern uint32_t g_samples_b[SAMPLES];
extern uint32_t g_samples_c[SAMPLES];
extern uint32_t g_samples_d[SAMPLES];
extern uint32_t g_overhead;
extern uint32_t g_khz;
extern uint32_t g_n;
extern uint32_t g_warmup;
extern uint32_t g_noise_ms;

static inline uint32_t Heavy(void) { return g_n < HEAVY ? g_n : HEAVY; }

uint32_t CyclesToNs(uint32_t cycles);
uint64_t CyclesToNs64(uint64_t cycles);
uint32_t MeasureKhz(uint32_t span_ms, uint32_t rounds);
uint32_t CalibrateOverhead(void);
void SortU32(uint32_t *a, uint32_t n);

void Report(const char *name, uint32_t n);
void ReportBuf(const char *name, const uint32_t *buf, uint32_t n);
void ReportUnit(const char *name, const uint32_t *buf, uint32_t n, Unit unit, bool sub_overhead);
void Point(const char *series, const char *xlabel, uint32_t x, const char *ylabel,
           uint64_t y_milli);

int OutOpen(const char *path, bool raw);
bool OutEnabled(void);
bool OutRaw(void);
const char *OutSectionName(void);
void OutSection(const char *name);
void OutPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void OutMeta(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void OutFlush(void);
void OutClose(void);
FsdConn *FsConn(void);

void BenchTimer(void);
void BenchNullSyscall(void);
void BenchYield(void);
void BenchSleep(void);
void BenchHandleOps(void);
void BenchHandleScaling(void);
void BenchCreateObjects(void);
void BenchMemMap(void);
void BenchTasks(void);
void BenchSpaces(void);
void BenchSched(void);
void RunIpc(void);
void RunIpcCross(void);
void BenchMemBandwidth(void);
void BenchMemLatency(void);
void BenchNoise(void);
void BenchFs(void);

#endif
