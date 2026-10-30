#include <stdio.h>
#include <string.h>
#include <zuzu/user_layout.h>
#include <zuzu/uptime.h>

#include "speedtest.h"

#define INJECT_MAX_PAGES 256u

void BenchTimer(void)
{
    for (uint32_t i = 0; i < g_n; i++) {
        uint32_t a = ArchMeasure();
        uint32_t b = ArchMeasure();
        g_samples[i] = b - a;
    }
    ReportUnit("cycle counter read", g_samples, g_n, UNIT_CYC, false);

    volatile uint64_t sink = 0;
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, sink += UptimeCounts());
    Report("uptime counts read", g_n);

    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, sink += UptimeUs());
    Report("uptime us (with divide)", g_n);
    (void)sink;
}

void BenchNullSyscall(void)
{
    for (uint32_t i = 0; i < g_warmup; i++)
        (void)HandleQuery(-1, QUERY_TYPE);
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, (void)HandleQuery(-1, QUERY_TYPE));
    Report("syscall (null)", g_n);

    Handle ev = CreateEvent();
    if (ev >= 0) {
        for (uint32_t i = 0; i < g_warmup; i++)
            (void)HandleQuery(ev, QUERY_TYPE);
        for (uint32_t i = 0; i < g_n; i++)
            TIMED(g_samples, i, (void)HandleQuery(ev, QUERY_TYPE));
        Report("handle query (valid)", g_n);
        HandleClose(ev);
    }
}

void BenchYield(void)
{
    for (uint32_t i = 0; i < g_warmup; i++)
        Yield();
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, Yield());
    Report("yield (no peer)", g_n);
}

void BenchSleep(void)
{
    static const uint32_t ms[] = {1, 2, 5, 10};
    for (uint32_t k = 0; k < ARRAY_LEN(ms); k++) {
        char name[40];
        uint32_t n = ms[k] >= 5 ? 100 : 200;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t t0 = UptimeUs();
            Sleep(ms[k]);
            uint64_t dt = UptimeUs() - t0;
            g_samples_b[i] = (uint32_t)dt;
            uint64_t want = (uint64_t)ms[k] * 1000u;
            g_samples_c[i] = dt > want ? (uint32_t)(dt - want) : 0;
        }
        snprintf(name, sizeof(name), "sleep %ums", (unsigned)ms[k]);
        ReportUnit(name, g_samples_b, n, UNIT_US, false);
        snprintf(name, sizeof(name), "sleep %ums overshoot", (unsigned)ms[k]);
        ReportUnit(name, g_samples_c, n, UNIT_US, false);
    }

    Handle ev = CreateEvent();
    if (ev < 0)
        return;
    for (uint32_t i = 0; i < 200; i++) {
        uint64_t t0 = UptimeUs();
        WaitOnEvent(ev, 1, 0, false);
        uint64_t dt = UptimeUs() - t0;
        g_samples_b[i] = dt > 1000u ? (uint32_t)(dt - 1000u) : 0;
    }
    ReportUnit("event wait 1ms timeout overshoot", g_samples_b, 200, UNIT_US, false);

    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, WaitOnEvent(ev, TIMEOUT_POLL, 0, false));
    Report("event wait poll (empty)", g_n);
    HandleClose(ev);
}

void BenchHandleOps(void)
{
    Handle ev = CreateEvent();
    if (ev < 0) {
        printf("handle bench setup failed\n");
        return;
    }
    for (uint32_t i = 0; i < g_warmup; i++) {
        SvcResult d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE);
        HandleClose((Handle)d.r1);
    }
    for (uint32_t i = 0; i < g_n; i++) {
        SvcResult d;
        TIMED(g_samples, i, d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE));
        TIMED(g_samples_b, i, HandleClose((Handle)d.r1));
    }
    Report("handle duplicate", g_n);
    ReportBuf("handle close", g_samples_b, g_n);

    for (uint32_t i = 0; i < g_warmup; i++)
        Signal(ev, 1, false);
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, Signal(ev, 1, false));
    Report("signal (no waiter)", g_n);

    for (uint32_t i = 0; i < g_n; i++) {
        Signal(ev, 1, false);
        TIMED(g_samples, i, WaitOnEvent(ev, TIMEOUT_POLL, 0, false));
    }
    Report("event wait (bits pending)", g_n);

    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, HandleRestrict(ev, PERM_ALL));
    Report("handle restrict", g_n);
    HandleClose(ev);
}

void BenchHandleScaling(void)
{
    static const uint32_t held[] = {0, 16, 64, 128, 256, 512, 768};
    static Handle extra[768];
    Handle ev = CreateEvent();
    if (ev < 0)
        return;
    uint32_t have = 0;
    uint32_t n = Heavy();
    for (uint32_t k = 0; k < ARRAY_LEN(held); k++) {
        while (have < held[k]) {
            SvcResult d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE);
            if (d.r0 != ZUZU_OK)
                break;
            extra[have++] = (Handle)d.r1;
        }
        if (have < held[k]) {
            printf("handle scaling: table full at %u\n", (unsigned)have);
            break;
        }
        for (uint32_t i = 0; i < n; i++) {
            SvcResult d;
            TIMED(g_samples, i, d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE));
            TIMED(g_samples_b, i, HandleClose((Handle)d.r1));
            TIMED(g_samples_c, i, (void)HandleQuery(ev, QUERY_TYPE));
        }
        char name[48];
        snprintf(name, sizeof(name), "dup @%u held", (unsigned)held[k]);
        Report(name, n);
        Point("dup vs handles held", "handles", held[k], "cycles", (uint64_t)g_samples[n / 2] * 1000);
        snprintf(name, sizeof(name), "close @%u held", (unsigned)held[k]);
        ReportBuf(name, g_samples_b, n);
        Point("close vs handles held", "handles", held[k], "cycles",
              (uint64_t)g_samples[n / 2] * 1000);
        snprintf(name, sizeof(name), "query @%u held", (unsigned)held[k]);
        ReportBuf(name, g_samples_c, n);
        Point("query vs handles held", "handles", held[k], "cycles",
              (uint64_t)g_samples[n / 2] * 1000);
    }
    for (uint32_t i = 0; i < have; i++)
        HandleClose(extra[i]);
    HandleClose(ev);
}

void BenchCreateObjects(void)
{
    uint32_t n = Heavy();
    for (uint32_t i = 0; i < n; i++) {
        Handle h;
        TIMED(g_samples, i, h = CreatePort());
        TIMED(g_samples_b, i, HandleClose(h));
    }
    Report("create port", n);
    ReportBuf("destroy port (close)", g_samples_b, n);

    for (uint32_t i = 0; i < n; i++) {
        Handle h;
        TIMED(g_samples, i, h = CreateEvent());
        TIMED(g_samples_b, i, HandleClose(h));
    }
    Report("create event", n);
    ReportBuf("destroy event (close)", g_samples_b, n);

    static const uint32_t pages[] = {1, 4, 16, 64, 256};
    for (uint32_t k = 0; k < ARRAY_LEN(pages); k++) {
        char name[40];
        uint32_t m = pages[k] >= 64 ? (n < 256 ? n : 256) : n;
        for (uint32_t i = 0; i < m; i++) {
            Handle h;
            TIMED(g_samples, i, h = CreateMem(pages[k], 0));
            if (h < 0) {
                printf("create mem %u pages failed: %d\n", (unsigned)pages[k], (int)h);
                return;
            }
            TIMED(g_samples_b, i, HandleClose(h));
        }
        snprintf(name, sizeof(name), "create mem %up", (unsigned)pages[k]);
        Report(name, m);
        Point("create mem vs pages", "pages", pages[k], "cycles",
              (uint64_t)g_samples[m / 2] * 1000);
        snprintf(name, sizeof(name), "free mem %up (close)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_b, m);
        Point("free mem vs pages", "pages", pages[k], "cycles", (uint64_t)g_samples[m / 2] * 1000);
    }
}

void BenchMemMap(void)
{
    static const uint32_t pages[] = {1, 4, 16, 64, 256};
    uint32_t n = Heavy();
    for (uint32_t k = 0; k < ARRAY_LEN(pages); k++) {
        uint32_t size = pages[k] * PAGE;
        uint32_t m = pages[k] >= 256 ? (n < 256 ? n : 256) : n;
        char name[40];
        for (uint32_t i = 0; i < m; i++) {
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
        Report(name, m);
        snprintf(name, sizeof(name), "touch %up (lazy faults)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_b, m);
        Point("lazy fault per page", "pages", pages[k], "cycles",
              (uint64_t)g_samples[m / 2] * 1000 / pages[k]);
        snprintf(name, sizeof(name), "mem unmap backed %up", (unsigned)pages[k]);
        ReportBuf(name, g_samples_c, m);
        Point("unmap per page", "pages", pages[k], "cycles",
              (uint64_t)g_samples[m / 2] * 1000 / pages[k]);
    }

    for (uint32_t k = 0; k < ARRAY_LEN(pages); k++) {
        uint32_t size = pages[k] * PAGE;
        uint32_t m = pages[k] >= 256 ? (n < 256 ? n : 256) : n;
        char name[40];
        Handle obj = CreateMem(pages[k], 0);
        if (obj < 0)
            return;
        for (uint32_t i = 0; i < m; i++) {
            volatile uint8_t *va;
            TIMED(g_samples, i, va = MemMap(obj, 0, PROT_RW));
            if (PtrIsErr((const void *)va)) {
                printf("map obj %up failed\n", (unsigned)pages[k]);
                HandleClose(obj);
                return;
            }
            TIMED(g_samples_b, i, for (uint32_t o = 0; o < size; o += PAGE) va[o] = 1);
            TIMED(g_samples_c, i, MemUnmap((void *)va));
        }
        HandleClose(obj);
        snprintf(name, sizeof(name), "mem map obj %up", (unsigned)pages[k]);
        Report(name, m);
        snprintf(name, sizeof(name), "touch obj %up (mapped)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_b, m);
        snprintf(name, sizeof(name), "mem unmap obj %up", (unsigned)pages[k]);
        ReportBuf(name, g_samples_c, m);
    }

    uint8_t *va = MemMapAnon(PAGE, 0, PROT_RW);
    if (PtrIsErr(va))
        return;
    va[0] = 0;
    for (uint32_t i = 0; i < g_n; i++)
        TIMED(g_samples, i, MemProtect((VirtAddr)va, PAGE, (i & 1) ? PROT_RW : PROT_READ));
    Report("mem protect 1p", g_n);
    MemUnmap(va);

    uint8_t *big = MemMapAnon(64 * PAGE, 0, PROT_RW);
    if (PtrIsErr(big))
        return;
    for (uint32_t o = 0; o < 64 * PAGE; o += PAGE)
        big[o] = 0;
    for (uint32_t i = 0; i < n; i++)
        TIMED(g_samples, i, MemProtect((VirtAddr)big, 64 * PAGE, (i & 1) ? PROT_RW : PROT_READ));
    Report("mem protect 64p", n);
    MemUnmap(big);
}

static void BlockEntry(Handle *port)
{
    WaitOn(*port, TIMEOUT_INFINITE);
    Quit(0);
}

void BenchTasks(void)
{
    Handle port = CreatePort();
    uint8_t *stack = MemMapAnon(SERVER_STACK, 0, PROT_RW);
    if (port < 0 || PtrIsErr(stack)) {
        printf("task bench setup failed\n");
        return;
    }
    stack[SERVER_STACK - 1] = 0;
    uint32_t n = Heavy();
    for (uint32_t i = 0; i < n; i++) {
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
    Report("create task", n);
    ReportBuf("task set_priority (frozen)", g_samples_b, n);
    ReportBuf("task start", g_samples_c, n);
    ReportBuf("task kill (blocked)", g_samples_d, n);
    HandleClose(port);
    MemUnmap(stack);
}

void BenchSpaces(void)
{
    uint8_t *src = MemMapAnon(INJECT_MAX_PAGES * PAGE, 0, PROT_RW);
    if (PtrIsErr(src)) {
        printf("space bench setup failed\n");
        return;
    }
    for (uint32_t o = 0; o < INJECT_MAX_PAGES * PAGE; o += PAGE)
        src[o] = 1;

    static const uint32_t pages[] = {0, 1, 4, 16, 64, 256};
    uint32_t heavy = Heavy();
    for (uint32_t k = 0; k < ARRAY_LEN(pages); k++) {
        uint32_t n = pages[k] >= 64 ? (heavy < 128 ? heavy : 128) : heavy;
        char name[48];
        Handle obj = -1;
        if (pages[k]) {
            obj = CreateMem(pages[k], 0);
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
            Point("injectobj vs pages", "pages", pages[k], "cycles",
                  (uint64_t)g_samples[n / 2] * 1000);
        }
        snprintf(name, sizeof(name), "destroy space (%up mapped)", (unsigned)pages[k]);
        ReportBuf(name, g_samples_c, n);
        Point("destroy space vs pages", "pages", pages[k], "cycles",
              (uint64_t)g_samples[n / 2] * 1000);
    }

    static const uint32_t inj[] = {1, 4, 16, 64};
    for (uint32_t k = 0; k < ARRAY_LEN(inj); k++) {
        uint32_t n = inj[k] >= 64 ? (heavy < 256 ? heavy : 256) : heavy;
        uint32_t got = 0;
        for (uint32_t i = 0; i < n; i++) {
            Handle sp = CreateSpace("speedtest-bench");
            if (sp < 0)
                break;
            Err rc;
            TIMED(g_samples_b, i,
                  rc = MemInjectBytes(sp, USER_ELF_BASE, src, inj[k] * PAGE, PROT_READ | PROT_EXEC));
            HandleDestroy(sp);
            if (rc != ZUZU_OK) {
                printf("injectbytes %up failed: %d\n", (unsigned)inj[k], (int)rc);
                break;
            }
            got++;
        }
        char name[48];
        snprintf(name, sizeof(name), "injectbytes %up (copy; exec)", (unsigned)inj[k]);
        ReportBuf(name, g_samples_b, got);
        if (got)
            Point("injectbytes vs pages", "pages", inj[k], "cycles",
                  (uint64_t)g_samples[got / 2] * 1000);
    }

    uint32_t got = 0;
    for (uint32_t i = 0; i < heavy; i++) {
        Handle sp = CreateSpace("speedtest-bench");
        Handle obj = CreateMem(16, 0);
        uint8_t *w = MemMap(obj, 0, PROT_RW);
        if (sp < 0 || obj < 0 || PtrIsErr(w))
            break;
        memcpy(w, src, 16 * PAGE);
        MemUnmap(w);
        TIMED(g_samples, i,
              MemInjectObj(sp, obj, USER_ELF_BASE, 0, 16 * PAGE, PROT_READ | PROT_EXEC));
        HandleClose(obj);
        HandleDestroy(sp);
        got++;
    }
    ReportBuf("injectobj 16p (exec; fresh object)", g_samples, got);
    MemUnmap(src);
}
