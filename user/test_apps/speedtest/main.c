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
#define HEAVY 1100
#define PAGE 4096u
#define INJECT_MAX_PAGES 256u

#define TIMED(buf, i, expr)                                                                        \
    do {                                                                                           \
        uint32_t t0_ = ArchMeasure();                                                              \
        expr;                                                                                      \
        (buf)[i] = ArchMeasure() - t0_;                                                            \
    } while (0)

static uint32_t g_samples[SAMPLES];
static uint32_t g_samples_b[SAMPLES];
static uint32_t g_samples_c[SAMPLES];
static uint32_t g_samples_d[SAMPLES];
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

static void ReportBuf(const char *name, const uint32_t *buf, uint32_t n)
{
    memcpy(g_samples, buf, n * sizeof(uint32_t));
    Report(name, n);
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
            memcpy(&first, GetMessageBox(), 4);
        if (first == MSG_QUIT) {
            Reply(0, -1);
            Quit(0);
        }
        r = FormatToPortWait(ReplyRecv(r.xlen, -1, s->port, TIMEOUT_INFINITE));
    }
}

static void BenchIpc(const char *kind, Handle port, uint32_t len)
{
    uint8_t *buf = GetMessageBox();
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
    memcpy(GetMessageBox(), &quit, 4);
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
    if (MemInjectBytes(space, USER_ELF_BASE, g_code_page, sizeof(g_code_page),
                       PROT_READ | PROT_EXEC) == ZUZU_OK &&
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

static void BenchHandleOps(void)
{
    Handle ev = CreateEvent();
    if (ev < 0) {
        printf("handle bench setup failed\n");
        return;
    }
    for (uint32_t i = 0; i < WARMUP; i++) {
        SvcResult d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE);
        HandleClose((Handle)d.r1);
    }
    for (uint32_t i = 0; i < SAMPLES; i++) {
        SvcResult d;
        TIMED(g_samples, i, d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE));
        TIMED(g_samples_b, i, HandleClose((Handle)d.r1));
    }
    Report("handle duplicate", SAMPLES);
    ReportBuf("handle close", g_samples_b, SAMPLES);

    for (uint32_t i = 0; i < WARMUP; i++)
        Signal(ev, 1, false);
    for (uint32_t i = 0; i < SAMPLES; i++)
        TIMED(g_samples, i, Signal(ev, 1, false));
    Report("signal (no waiter)", SAMPLES);

    for (uint32_t i = 0; i < SAMPLES; i++)
        TIMED(g_samples, i, HandleRestrict(ev, PERM_ALL));
    Report("handle restrict", SAMPLES);
    HandleClose(ev);
}

static void BenchCreateObjects(void)
{
    for (uint32_t i = 0; i < HEAVY; i++) {
        Handle h;
        TIMED(g_samples, i, h = CreatePort());
        TIMED(g_samples_b, i, HandleClose(h));
    }
    Report("create port", HEAVY);
    ReportBuf("destroy port (close)", g_samples_b, HEAVY);

    for (uint32_t i = 0; i < HEAVY; i++) {
        Handle h;
        TIMED(g_samples, i, h = CreateEvent());
        TIMED(g_samples_b, i, HandleClose(h));
    }
    Report("create event", HEAVY);
    ReportBuf("destroy event (close)", g_samples_b, HEAVY);

    static const uint32_t pages[] = {1, 16, 256};
    for (uint32_t k = 0; k < sizeof(pages) / sizeof(pages[0]); k++) {
        char name[40];
        uint32_t n = pages[k] >= 256 ? 256 : HEAVY;
        for (uint32_t i = 0; i < n; i++) {
            Handle h;
            TIMED(g_samples, i, h = CreateMem(pages[k]));
            if (h < 0) {
                printf("create mem %u pages failed: %d\n", (unsigned)pages[k], (int)h);
                return;
            }
            TIMED(g_samples_b, i, HandleClose(h));
        }
        snprintf(name, sizeof(name), "create mem %up", (unsigned)pages[k]);
        Report(name, n);
        snprintf(name, sizeof(name), "free mem %up (close)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_b, n);
    }
}

static void BenchMemMap(void)
{
    static const uint32_t pages[] = {1, 16, 64};
    for (uint32_t k = 0; k < sizeof(pages) / sizeof(pages[0]); k++) {
        uint32_t size = pages[k] * PAGE;
        char name[40];
        for (uint32_t i = 0; i < HEAVY; i++) {
            volatile uint8_t *va;
            TIMED(g_samples, i, va = MemMapAnon(size, 0, PROT_RW));
            if (PtrIsErr((const void *)va)) {
                printf("map %up failed\n", (unsigned)pages[k]);
                return;
            }
            TIMED(g_samples_b, i, for (uint32_t o = 0; o < size; o += PAGE) va[o] = 1);
            TIMED(g_samples_c, i, MemUnmap((void *)va));
        }
        snprintf(name, sizeof(name), "mem map anon %up", (unsigned)pages[k]);
        Report(name, HEAVY);
        snprintf(name, sizeof(name), "touch %up (lazy faults)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_b, HEAVY);
        snprintf(name, sizeof(name), "mem unmap backed %up", (unsigned)pages[k]);
        ReportBuf(name, g_samples_c, HEAVY);
    }

    uint8_t *va = MemMapAnon(PAGE, 0, PROT_RW);
    if (PtrIsErr(va))
        return;
    for (uint32_t i = 0; i < SAMPLES; i++)
        TIMED(g_samples, i, MemProtect((VirtAddr)va, PAGE, (i & 1) ? PROT_RW : PROT_READ));
    Report("mem protect 1p", SAMPLES);
    MemUnmap(va);
}

static void BlockEntry(Handle *port)
{
    WaitOn(*port, TIMEOUT_INFINITE);
    Quit(0);
}

static void BenchTasks(void)
{
    Handle port = CreatePort();
    uint8_t *stack = MemMapAnon(SERVER_STACK, 0, PROT_RW);
    if (port < 0 || PtrIsErr(stack)) {
        printf("task bench setup failed\n");
        return;
    }
    stack[0] = 0;
    for (uint32_t i = 0; i < HEAVY; i++) {
        Handle t;
        TIMED(g_samples, i, t = CreateTask(-1));
        if (t < 0) {
            printf("create task failed: %d\n", (int)t);
            return;
        }
        TIMED(g_samples_b, i, TaskSetPriority(t, 1));
        TIMED(g_samples_c, i,
              TaskStart(t, (void *)BlockEntry, stack + SERVER_STACK, (uint32_t)(uintptr_t)&port,
                        0));
        Yield();
        TIMED(g_samples_d, i, TaskKill(t));
        HandleClose(t);
    }
    Report("create task", HEAVY);
    ReportBuf("task set_priority (frozen)", g_samples_b, HEAVY);
    ReportBuf("task start", g_samples_c, HEAVY);
    ReportBuf("task kill (blocked)", g_samples_d, HEAVY);
    HandleClose(port);
    MemUnmap(stack);
}

static void BenchSpaces(void)
{
    uint8_t *src = MemMapAnon(INJECT_MAX_PAGES * PAGE, 0, PROT_RW);
    if (PtrIsErr(src)) {
        printf("space bench setup failed\n");
        return;
    }
    for (uint32_t o = 0; o < INJECT_MAX_PAGES * PAGE; o += PAGE)
        src[o] = 1;

    static const uint32_t pages[] = {0, 1, 16, 256};
    for (uint32_t k = 0; k < sizeof(pages) / sizeof(pages[0]); k++) {
        uint32_t n = pages[k] >= 256 ? 128 : HEAVY;
        char name[48];
        Handle obj = -1;
        if (pages[k]) {
            obj = CreateMem(pages[k]);
            uint8_t *w = MemMap(obj, 0, PROT_RW);
            if (obj < 0 || PtrIsErr(w)) {
                printf("injectobj setup failed\n");
                return;
            }
            memcpy(w, src, pages[k] * PAGE);
            MemUnmap(w);
        }
        for (uint32_t i = 0; i < n; i++) {
            Handle sp;
            TIMED(g_samples, i, sp = CreateSpace("speedtest-bench"));
            if (sp < 0) {
                printf("create space failed: %d\n", (int)sp);
                return;
            }
            if (pages[k]) {
                TIMED(g_samples_b, i,
                      MemInjectObj(sp, obj, USER_ELF_BASE, 0, pages[k] * PAGE, PROT_RW));
            } else {
                g_samples_b[i] = 0;
            }
            TIMED(g_samples_c, i, HandleDestroy(sp));
        }
        if (obj >= 0)
            HandleClose(obj);
        snprintf(name, sizeof(name), "create space (%up mapped)", (unsigned)pages[k]);
        Report(name, n);
        if (pages[k]) {
            snprintf(name, sizeof(name), "injectobj %up (rw)", (unsigned)pages[k]);
            ReportBuf(name, g_samples_b, n);
        }
        snprintf(name, sizeof(name), "destroy space (%up mapped)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_c, n);
    }

    for (uint32_t i = 0; i < HEAVY; i++) {
        Handle sp = CreateSpace("speedtest-bench");
        Handle obj = CreateMem(16);
        uint8_t *w = MemMap(obj, 0, PROT_RW);
        if (sp < 0 || obj < 0 || PtrIsErr(w))
            break;
        memcpy(w, src, 16 * PAGE);
        MemUnmap(w);
        TIMED(g_samples, i,
              MemInjectObj(sp, obj, USER_ELF_BASE, 0, 16 * PAGE, PROT_READ | PROT_EXEC));
        HandleClose(obj);
        HandleDestroy(sp);
    }
    ReportBuf("injectobj 16p (exec, fresh object)", g_samples, HEAVY);
    MemUnmap(src);
}

static void RunSection(const char *title, void (*fn)(void))
{
    BenchReset();
    fn();
    printf("-- kernel stats: %s\n", title);
    Sleep(250);
    BenchDump();
    Sleep(250);
}

int main(void)
{
    if (stdio_open_tty() != 0)
        return 1;

    g_khz = CalibrateKhz();
    g_overhead = CalibrateOverhead();
    printf("speedtest: %u samples, %u warmup, clock %u.%03u MHz, timer overhead %u cyc\n", SAMPLES,
           WARMUP, g_khz / 1000, g_khz % 1000, g_overhead);

    RunSection("null syscall", BenchNullSyscall);
    RunSection("yield", BenchYield);
    RunSection("handle ops", BenchHandleOps);
    RunSection("create objects", BenchCreateObjects);
    RunSection("mem map", BenchMemMap);
    RunSection("tasks", BenchTasks);
    RunSection("spaces", BenchSpaces);
    RunSection("ipc same-space", RunIpc);
    RunSection("ipc cross-space", RunIpcCross);
    return 0;
}
