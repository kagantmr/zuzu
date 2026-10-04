#include <arch/cycles.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define SAMPLES 8192
#define WARMUP 1024
#define CAL_ROUNDS 5
#define SERVER_STACK (16 * 1024)
#define MSG_QUIT 0xFFFFFFFFu

static uint32_t g_samples[SAMPLES];
static uint32_t g_overhead;
static uint32_t g_khz;

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

static uint32_t Median(uint32_t *a, uint32_t n)
{
    SortU32(a, n);
    return a[n / 2];
}

static uint32_t CyclesToNs(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000u) / g_khz);
}

static inline uint32_t Ticks(void) { return ((volatile const Syspage *)SYSPAGE)->uptime_ticks; }

static uint32_t CalibrateOverhead(void)
{
    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t a = ArchMeasure();
        uint32_t b = ArchMeasure();
        g_samples[i] = b - a;
    }
    return Median(g_samples, SAMPLES);
}

static uint32_t CalibrateKhz(void)
{
    uint32_t hz = ((const Syspage *)SYSPAGE)->tick_hz;
    uint32_t span = hz / 10;
    if (span == 0)
        span = 1;
    uint32_t est[CAL_ROUNDS];

    for (int r = 0; r < CAL_ROUNDS; r++) {
        uint32_t t0 = Ticks();
        while (Ticks() == t0)
            ;
        uint32_t c0 = ArchMeasure();
        uint32_t t1 = Ticks();
        while (Ticks() - t1 < span)
            ;
        uint32_t c1 = ArchMeasure();
        uint32_t t2 = Ticks();
        est[r] = (uint32_t)(((uint64_t)(c1 - c0) * hz) / ((uint64_t)(t2 - t1) * 1000u));
    }
    return Median(est, CAL_ROUNDS);
}

static void Report(const char *name, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        g_samples[i] = g_samples[i] > g_overhead ? g_samples[i] - g_overhead : 0;

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++)
        sum += g_samples[i];
    uint32_t mean = (uint32_t)(sum / n);

    uint64_t var = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t d = (int64_t)g_samples[i] - mean;
        var += (uint64_t)(d * d);
    }
    var /= n;
    uint32_t sd = 0;
    while ((uint64_t)(sd + 1) * (sd + 1) <= var)
        sd++;

    SortU32(g_samples, n);
    uint32_t min = g_samples[0];
    uint32_t med = g_samples[n / 2];
    uint32_t p99 = g_samples[(n * 99) / 100];
    uint32_t max = g_samples[n - 1];

    printf("%-22s min %6u  med %6u  p99 %6u  max %7u  mean %6u  sd %5u cyc | med %6u ns\n", name,
           min, med, p99, max, mean, sd, CyclesToNs(med));
}

static void BenchNullSyscall(void)
{
    for (uint32_t i = 0; i < WARMUP; i++)
        (void)HandleQuery(-1, QUERY_TYPE);
    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t s = ArchMeasure();
        (void)HandleQuery(-1, QUERY_TYPE);
        g_samples[i] = ArchMeasure() - s;
    }
    Report("syscall (null)", SAMPLES);
}

static void BenchYield(void)
{
    for (uint32_t i = 0; i < WARMUP; i++)
        Yield();
    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t s = ArchMeasure();
        Yield();
        g_samples[i] = ArchMeasure() - s;
    }
    Report("yield (no peer)", SAMPLES);
}

typedef struct {
    Handle port;
} Server;

static void ServerEntry(Server *s)
{
    PortWaitResult r = FormatToPortWait(WaitOn(s->port, TIMEOUT_INFINITE));
    for (;;) {
        if (r.status != ZUZU_OK)
            Quit(r.status);
        uint32_t first = 0;
        if (r.xlen >= 4)
            memcpy(&first, MessageBuf(), 4);
        if (first == MSG_QUIT) {
            Reply(0, -1);
            Quit(0);
        }
        r = FormatToPortWait(ReplyRecv(r.xlen, -1, s->port, TIMEOUT_INFINITE));
    }
}

static void BenchIpc(const char *kind, Handle port, uint32_t len)
{
    uint8_t *buf = MessageBuf();
    memset(buf, 0xA5, len < 4 ? 4 : len);
    buf[0] = 0;
    buf[1] = buf[2] = buf[3] = 0;

    for (uint32_t i = 0; i < WARMUP; i++)
        (void)Call(port, len, -1);
    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t s = ArchMeasure();
        SvcResult r = Call(port, len, -1);
        g_samples[i] = ArchMeasure() - s;
        if (r.r0 != ZUZU_OK) {
            printf("ipc call failed: %d\n", (int)r.r0);
            return;
        }
    }
    char name[32];
    snprintf(name, sizeof(name), "%s rtt %uB", kind, (unsigned)len);
    Report(name, SAMPLES);
}

static void RunIpc(void)
{
    Server srv = {.port = CreatePort()};
    uint8_t *stack = MemMapAnon(SERVER_STACK, 0, PROT_RW);
    Handle task = CreateTask(-1);
    if (srv.port < 0 || PtrIsErr(stack) || task < 0) {
        printf("ipc setup failed\n");
        return;
    }
    if (TaskStart(task, (void *)ServerEntry, stack + SERVER_STACK, (uint32_t)(uintptr_t)&srv, 0) !=
        ZUZU_OK) {
        printf("ipc server start failed\n");
        return;
    }

    BenchIpc("ipc same-space", srv.port, 4);
    BenchIpc("ipc same-space", srv.port, 64);
    BenchIpc("ipc same-space", srv.port, 256);

    uint32_t quit = MSG_QUIT;
    memcpy(MessageBuf(), &quit, 4);
    Call(srv.port, 4, -1);
    WaitOn(task, 1000);
    HandleClose(task);
    HandleClose(srv.port);
    MemUnmap(stack);
}

static const uint32_t kEchoCode[] = {
    0xE1A04000, 0xE1A00004, 0xE3E01000, 0xEF000007, 0xE3500000, 0x1A000005, 0xE1A00002,
    0xE3E01000, 0xE1A02004, 0xE3E03000, 0xEF000006, 0xEAFFFFF7, 0xEF000000,
};

static uint8_t g_code_page[4096] __attribute__((aligned(4096)));

static void RunIpcCross(void)
{
    Handle port = CreatePort();
    Handle space = CreateSpace("speedtest-echo");
    if (port < 0 || space < 0) {
        printf("cross-space setup failed\n");
        return;
    }
    memcpy(g_code_page, kEchoCode, sizeof(kEchoCode));
    Handle task = -1;
    SvcResult g = HandleGrant(port, space, PERM_ALL);
    if (MemInject(space, USER_ELF_BASE, g_code_page, sizeof(g_code_page), PROT_READ | PROT_EXEC,
                  0) == ZUZU_OK &&
        g.r0 == ZUZU_OK && (task = CreateTask(space)) >= 0 &&
        TaskStart(task, (void *)USER_ELF_BASE, (void *)USR_SP, (uint32_t)g.r1, 0) == ZUZU_OK) {
        BenchIpc("ipc cross-space", port, 4);
        BenchIpc("ipc cross-space", port, 64);
        BenchIpc("ipc cross-space", port, 256);
    } else {
        printf("cross-space setup failed\n");
    }
    if (task >= 0)
        HandleClose(task);
    HandleDestroy(space);
    HandleClose(port);
}

int main(void)
{
    if (stdio_open_tty() != 0)
        return 1;

    g_khz = CalibrateKhz();
    g_overhead = CalibrateOverhead();
    printf("speedtest: %u samples, %u warmup, clock %u.%03u MHz, timer overhead %u cyc\n", SAMPLES,
           WARMUP, g_khz / 1000, g_khz % 1000, g_overhead);

    BenchNullSyscall();
    BenchYield();
    RunIpc();
    RunIpcCross();
    return 0;
}
