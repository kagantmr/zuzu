#include <stdio.h>
#include <string.h>

#include "speedtest.h"

#define BASE_PRIO 1u

typedef struct {
    Handle task;
    uint8_t *stack;
} Peer;

typedef struct {
    Handle ev_a;
    Handle ev_b;
    volatile uint32_t stop;
    volatile uint32_t wake;
} PingPong;

static bool PeerStart(Peer *p, void (*entry)(PingPong *), PingPong *arg, int prio_delta)
{
    p->stack = MemMapAnon(SERVER_STACK, 0, PROT_RW);
    p->task = CreateTask(-1);
    if (PtrIsErr(p->stack) || p->task < 0)
        return false;
    p->stack[SERVER_STACK - 1] = 0;
    if (prio_delta) {
        if (TaskSetPriority(p->task, BASE_PRIO + (uint32_t)prio_delta) != ZUZU_OK) {
            HandleClose(p->task);
            MemUnmap(p->stack);
            p->task = -1;
            return false;
        }
    }
    return TaskStart(p->task, (void *)entry, p->stack + SERVER_STACK, (uint32_t)(uintptr_t)arg,
                     0) == ZUZU_OK;
}

static void PeerReap(Peer *p)
{
    if (p->task >= 0) {
        if (WaitOn(p->task, 1000).r0 != ZUZU_OK)
            TaskKill(p->task);
        HandleClose(p->task);
    }
    if (!PtrIsErr(p->stack))
        MemUnmap(p->stack);
}

static void YieldPeer(PingPong *pp)
{
    while (!pp->stop)
        Yield();
    Quit(0);
}

static void EventEchoPeer(PingPong *pp)
{
    for (;;) {
        WaitOnEvent(pp->ev_a, TIMEOUT_INFINITE, 1, false);
        if (pp->stop)
            Quit(0);
        Signal(pp->ev_b, 1, false);
    }
}

static void WakeStampPeer(PingPong *pp)
{
    for (;;) {
        WaitOnEvent(pp->ev_a, TIMEOUT_INFINITE, 1, false);
        pp->wake = ArchMeasure();
        if (pp->stop)
            Quit(0);
    }
}

static void BenchYieldPingPong(void)
{
    PingPong pp = {0};
    Peer p;
    if (!PeerStart(&p, YieldPeer, &pp, 0)) {
        printf("yield ping-pong setup failed\n");
        return;
    }
    for (uint32_t i = 0; i < g_warmup; i++)
        Yield();
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, Yield());
    pp.stop = 1;
    Report("yield ping-pong (2 switches)", g_n);
    PeerReap(&p);
}

static void BenchEventPingPong(int prio_delta, const char *name)
{
    PingPong pp = {.ev_a = CreateEvent(), .ev_b = CreateEvent()};
    Peer p;
    if (pp.ev_a < 0 || pp.ev_b < 0 || !PeerStart(&p, EventEchoPeer, &pp, prio_delta)) {
        printf("%s: setup failed\n", name);
        HandleClose(pp.ev_a);
        HandleClose(pp.ev_b);
        return;
    }
    for (uint32_t i = 0; i < g_warmup; i++) {
        Signal(pp.ev_a, 1, false);
        WaitOnEvent(pp.ev_b, TIMEOUT_INFINITE, 1, false);
    }
    for (uint32_t i = 0; i < g_n; i++) {
        uint32_t t0 = ArchMeasure();
        Signal(pp.ev_a, 1, false);
        uint32_t t1 = ArchMeasure();
        WaitOnEvent(pp.ev_b, TIMEOUT_INFINITE, 1, false);
        uint32_t t2 = ArchMeasure();
        g_samples[i] = t2 - t0;
        g_samples_b[i] = t1 - t0;
    }
    pp.stop = 1;
    Signal(pp.ev_a, 1, false);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s rtt", name);
    Report(buf, g_n);
    snprintf(buf, sizeof(buf), "%s signal", name);
    ReportBuf(buf, g_samples_b, g_n);
    PeerReap(&p);
    HandleClose(pp.ev_a);
    HandleClose(pp.ev_b);
}

static void BenchWakeLatency(void)
{
    PingPong pp = {.ev_a = CreateEvent()};
    Peer p;
    if (pp.ev_a < 0 || !PeerStart(&p, WakeStampPeer, &pp, 1)) {
        printf("wake latency: needs a higher-priority peer, skipped\n");
        HandleClose(pp.ev_a);
        return;
    }
    Yield();
    for (uint32_t i = 0; i < g_warmup; i++)
        Signal(pp.ev_a, 1, false);
    for (uint32_t i = 0; i < g_n; i++) {
        uint32_t t0 = ArchMeasure();
        Signal(pp.ev_a, 1, false);
        uint32_t t1 = ArchMeasure();
        g_samples[i] = pp.wake - t0;
        g_samples_b[i] = t1 - t0;
    }
    pp.stop = 1;
    Signal(pp.ev_a, 1, false);
    Report("signal -> hi-prio wake", g_n);
    ReportBuf("signal + preempt + back", g_samples_b, g_n);
    PeerReap(&p);
    HandleClose(pp.ev_a);
}

void BenchSched(void)
{
    if (TaskSetPriority(-1, BASE_PRIO) != ZUZU_OK)
        printf("sched: could not pin own priority to %u\n", (unsigned)BASE_PRIO);
    OutMeta("base_prio", "%u", (unsigned)BASE_PRIO);
    BenchYieldPingPong();
    BenchEventPingPong(0, "event ping-pong same-prio");
    BenchEventPingPong(1, "event ping-pong hi-prio peer");
    BenchWakeLatency();
}
