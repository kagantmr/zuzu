#include <stdio.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/user_layout.h>

#include "speedtest.h"

static const uint32_t kSizes[] = {0, 4, 16, 64, 128, 256, 384, 512};

typedef struct {
    Handle port;
} Server;

static void ServerEntry(Server *s)
{
    PortWaitResult r = FormatToPortWait(WaitOn(s->port, TIMEOUT_INFINITE));
    for (;;) {
        if (r.status != ZUZU_OK)
            Quit(r.status);
        if (r.granted >= 0)
            HandleClose(r.granted);
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

static bool BenchIpc(const char *kind, Handle port, uint32_t len, Handle grant)
{
    uint8_t *buf = GetMessageBox();
    memset(buf, 0xA5, len < 4 ? 4 : len);
    buf[0] = buf[1] = buf[2] = buf[3] = 0;

    for (uint32_t i = 0; i < g_warmup; i++)
        (void)Call(port, len, grant);
    for (uint32_t i = 0; i < g_n; i++) {
        uint32_t s = ArchMeasure();
        SvcResult r = Call(port, len, grant);
        g_samples[i] = ArchMeasure() - s;
        if (r.r0 != ZUZU_OK) {
            printf("ipc call failed: %d\n", (int)r.r0);
            return false;
        }
    }
    char name[48];
    char series[48];
    snprintf(name, sizeof(name), "%s rtt %uB%s", kind, (unsigned)len, grant >= 0 ? " +grant" : "");
    Report(name, g_n);
    if (grant >= 0)
        return true;
    uint32_t med = g_samples[g_n / 2];
    snprintf(series, sizeof(series), "%s rtt", kind);
    Point(series, "bytes", len, "ns", CyclesToNs64((uint64_t)med * 1000));
    uint64_t ns_milli = CyclesToNs64((uint64_t)med * 1000);
    if (len && ns_milli) {
        snprintf(series, sizeof(series), "%s throughput", kind);
        Point(series, "bytes", len, "MB/s", (uint64_t)2 * len * 1000000000ull / ns_milli);
    }
    return true;
}

static void SweepIpc(const char *kind, Handle port)
{
    for (uint32_t k = 0; k < ARRAY_LEN(kSizes); k++)
        if (!BenchIpc(kind, port, kSizes[k], -1))
            return;
}

void RunIpc(void)
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

    SweepIpc("ipc same-space", srv.port);

    Handle ev = CreateEvent();
    if (ev >= 0) {
        BenchIpc("ipc same-space", srv.port, 4, ev);
        HandleClose(ev);
    }

    uint32_t quit = MSG_QUIT;
    memcpy(GetMessageBox(), &quit, 4);
    Call(srv.port, 4, -1);
    WaitOn(task, 1000);
    HandleClose(task);
    HandleClose(srv.port);
    MemUnmap(stack);
}

#define SYS(n) (0xE3A0C000u | (uint32_t)(n)), (0xEF000000u | ARCH_SVC_MAGIC)

static const uint32_t kEchoCode[] = {
    0xE1A04000, 0xE1A00004, 0xE3E01000, SYS(SVC_WAITON), 0xE3500000, 0x1A000006, 0xE1A00002,
    0xE3E01000, 0xE1A02004, 0xE3E03000, SYS(SVC_REPLY),  0xEAFFFFF6, SYS(SVC_QUIT),
};

static uint8_t g_code_page[4096] __attribute__((aligned(4096)));

void RunIpcCross(void)
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
    Err inj = MemInjectBytes(space, USER_ELF_BASE, g_code_page, sizeof(g_code_page),
                             PROT_READ | PROT_EXEC);
    Err start = ERR_BADARG;
    if (inj == ZUZU_OK && g.r0 == ZUZU_OK && (task = CreateTask(space)) >= 0)
        start = TaskStart(task, (void *)USER_ELF_BASE, (void *)USR_SP, (uint32_t)g.r1, 0);
    if (start == ZUZU_OK) {
        SweepIpc("ipc cross-space", port);
    } else {
        printf("cross-space setup failed: grant %d inject %d task %d start %d\n", (int)g.r0,
               (int)inj, (int)task, (int)start);
    }
    if (task >= 0)
        HandleClose(task);
    HandleDestroy(space);
    HandleClose(port);
}
