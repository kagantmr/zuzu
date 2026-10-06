/* zztest - Prowl syscall ABI regression suite.
 *
 * Exercises each of the 13 Prowl syscalls through include/zuzu/zuzu.h: the
 * success paths and the documented error cases. Results go to the kernel
 * console via UserspaceDebugLog (DEBUG builds only); the exit status is the
 * number of failed checks.
 *
 * Same-space worker threads play the IPC/event peers. Anything that needs a
 * second address space (faults, space lifecycle, MemInject) injects a few
 * hand-assembled ARM words into a kitten Space, so the suite needs neither
 * the initrd nor a second binary. A faulting kitten makes the kernel print
 * an "Oops!" register dump: that is expected.
 *
 * Not covered (no way to reach it from an ordinary Space): device MemObjects
 * (MemMap/BindIrq/IrqRearm on a real device), BindMemMgmt delivery, and
 * marker stamping from a non-owner Space.
 */
/* zztest reports through UserspaceDebugLog, which is off by default. */
#define UDBG_ENABLED 1

#include <arch/svc.h>
#include <stdatomic.h>
#include <string.h>
#include <sync/primitives.h>
#include <util/msg.h>
#include <util/tls.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <zuzu/udbg.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define WORKER_STACK (16 * 1024)
#define MAX_WORKERS 16
#define JOIN_TIMEOUT_MS 3000

/* HandleType is kernel-private (kernel/space/handle.h); QUERY_TYPE returns
 * these raw values. */
enum { ZH_PORT = 1, ZH_MEM, ZH_EVENT, ZH_TASK, ZH_SPACE };

/* ---------------- harness ---------------- */

#define MAX_SECTIONS 16
typedef struct {
    const char *name;
    int pass;
    int fail;
} Section;

static Section g_sections[MAX_SECTIONS];
static int g_cur = -1;
static bool g_verbose = true;

static uint32_t PagesFree(void);
static void SleepAndSettle(void);

/* MemMap/MemMapAnon return a pointer; an Err comes back as a small negative one. */
#define PtrErr(p) ((Err)(intptr_t)(p))
static uint32_t g_section_pages;

static void EndSection(void)
{
    if (g_cur < 0)
        return;
    SleepAndSettle();
    int32_t delta = (int32_t)g_section_pages - (int32_t)PagesFree();
    if (delta != 0)
        UserspaceDebugLog("zztest: note: section '%s' changed free pages by %d",
                          g_sections[g_cur].name, -delta);
}

static void BeginSection(const char *name)
{
    EndSection();
    g_section_pages = PagesFree();
    g_cur++;
    g_sections[g_cur] = (Section){.name = name};
    UserspaceDebugLog("zztest: --- %s ---", name);
}

static void Check(bool cond, const char *msg)
{
    if (cond) {
        g_sections[g_cur].pass++;
        if (g_verbose)
            UserspaceDebugLog("zztest: ok:   %s", msg);
    } else {
        g_sections[g_cur].fail++;
        UserspaceDebugLog("zztest: FAIL: %s", msg);
    }
}

static void CheckEq(int32_t got, int32_t want, const char *msg)
{
    if (got == want) {
        g_sections[g_cur].pass++;
        if (g_verbose)
            UserspaceDebugLog("zztest: ok:   %s", msg);
    } else {
        g_sections[g_cur].fail++;
        UserspaceDebugLog("zztest: FAIL: %s (got %d, want %d)", msg, got, want);
    }
}

static uint32_t PagesFree(void) { return ((const Syspage *)SYSPAGE)->mem_free_kb / 4; }

static uint64_t NowMs(void)
{
    const Syspage *sp = (const Syspage *)SYSPAGE;
    return sp->uptime_ticks * 1000u / sp->tick_hz;
}

static uint64_t TickMs(void) { return 1000u / ((const Syspage *)SYSPAGE)->tick_hz; }

static void SleepAndSettle(void)
{
    Sleep(100); /* lets the deferred thread reaper run before counting pages */
}

/* ---------------- worker threads (same Space) ---------------- */

typedef int32_t (*WorkerFn)(void *);

typedef struct {
    WorkerFn fn;
    void *arg;
    uint8_t *stack;
    Handle task;
    bool used;
} Worker;

static Worker g_workers[MAX_WORKERS];

static void WorkerEntry(Worker *w) { Quit(w->fn(w->arg)); }

static Worker *WorkerStart(WorkerFn fn, void *arg)
{
    Worker *w = NULL;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!g_workers[i].used) {
            w = &g_workers[i];
            break;
        }
    }
    if (!w)
        return NULL;

    uint8_t *stack = MemMapAnon(WORKER_STACK, 0, PROT_RW);
    if (PtrIsErr(stack))
        return NULL;
    Handle task = CreateTask(-1);
    if (task < 0) {
        MemUnmap(stack);
        return NULL;
    }
    *w = (Worker){.fn = fn, .arg = arg, .stack = stack, .task = task, .used = true};
    if (TaskStart(task, (void *)WorkerEntry, stack + WORKER_STACK, (uint32_t)(uintptr_t)w, 0) !=
        ZUZU_OK) {
        HandleClose(task);
        MemUnmap(stack);
        w->used = false;
        return NULL;
    }
    return w;
}

/* Waits for the worker to exit, kills it if it does not, and releases its
 * stack and handle. Returns its exit status, or an Err if it never exited. */
static int32_t WorkerJoin(Worker *w)
{
    TaskWaitResult tw = FormatToTaskWait(WaitOn(w->task, JOIN_TIMEOUT_MS));
    int32_t result = tw.status == ZUZU_OK ? tw.value : tw.status;
    if (tw.status != ZUZU_OK)
        TaskKill(w->task);
    HandleClose(w->task);
    MemUnmap(w->stack);
    w->used = false;
    return result;
}

/* ---------------- kitten Spaces from raw ARM code ---------------- */

static const uint32_t kCodeLoop[] = {0xEAFFFFFE};                      /* b . */
static const uint32_t kCodeQuit42[] = {0xE3A0002A, 0xEF000000};        /* mov r0,#42; svc QUIT */
static const uint32_t kCodeFaultData[] = {0xE3A00000, 0xE5901000};     /* mov r0,#0; ldr r1,[r0] */
static const uint32_t kCodeFaultPrefetch[] = {0xE3A00000, 0xE12FFF10}; /* mov r0,#0; bx r0 */
static const uint32_t kCodeFaultUndef[] = {0xE7F000F0};                /* udf */
static const uint32_t kCodeReadSyspage[] = {0xE3A01A01, 0xE5910000,
                                            0xEF000000}; /* mov r1,#0x1000; ldr r0,[r1]; svc QUIT */
static const uint32_t kCodeReadBootInfo[] = {0xE3A01A02, 0xE5910000,
                                             0xEF000000}; /* mov r1,#0x2000; ldr r0,[r1]; ... */
static const uint32_t kCodeReadKernel[] = {0xE3A01103, 0xE5910000,
                                           0xEF000000}; /* mov r1,#0xC0000000; ldr r0,[r1] */
static const uint32_t kCodeWriteCode[] = {0xE3A01801, 0xE5810000,
                                          0xEF000000}; /* mov r1,#0x10000; str r0,[r1] (RX page) */
static const uint32_t kCodeExecData[] = {0xE3A01101,
                                         0xE12FFF11}; /* mov r1,#0x40000000; bx r1 (RW, no exec) */
static const uint32_t kCodeStack[] = {
    0xE3A02009, 0xE50D2008, 0xE51D0008,
    0xEF000000}; /* mov r2,#9; str r2,[sp,#-8]; ldr r0,[sp,#-8]; QUIT */
static const uint32_t kCodeYieldLoop[] = {0xE3A04064, 0xEF000001, 0xE2544001, 0x1AFFFFFC,
                                          0xE3A00005, 0xEF000000}; /* 100x YIELD, QUIT 5 */
static const uint32_t kCodeSleepQuit[] = {0xE3A00014, 0xEF000002, 0xE3A00006,
                                          0xEF000000}; /* SLEEP 20; QUIT 6 */
/* ldr r1,[r0] (r0 comes in as the start argument); mov r0,#7; svc QUIT */
static const uint32_t kCodeLoadThenQuit7[] = {0xE5901000, 0xE3A00007, 0xEF000000};
/* r0 = a space handle (start argument); quit with ManageHandle(r0, CLOSE / DESTROY) */
static const uint32_t kCodeCloseHandle[] = {0xE3A01002, 0xEF000008, 0xEF000000};
static const uint32_t kCodeDestroyHandle[] = {0xE3A01004, 0xEF000008, 0xEF000000};

static uint8_t g_code_page[4096] __attribute__((aligned(4096)));

typedef struct {
    Handle space;
    Handle task;
} Kitten;

#define KITTEN_DATA_VA 0x40000000u

/* Creates a Space, injects `code` at USER_ELF_BASE (plus a zeroed read-write
 * page at KITTEN_DATA_VA if `with_data`) and a (frozen) Task. */
static Err KittenCreateEx(Kitten *k, const uint32_t *code, size_t bytes, bool with_data)
{
    k->space = CreateSpace("zz-kitten");
    k->task = -1;
    if (k->space < 0)
        return (Err)k->space;
    memset(g_code_page, 0, sizeof(g_code_page));
    memcpy(g_code_page, code, bytes);
    Err rc = ZUZU_OK;
    if (with_data)
        rc = MemInject(k->space, KITTEN_DATA_VA, NULL, 4096, PROT_RW, ASINJECT_FLAG_RESERVE);
    if (rc == ZUZU_OK)
        rc = MemInject(k->space, USER_ELF_BASE, g_code_page, sizeof(g_code_page),
                       PROT_READ | PROT_EXEC, 0);
    if (rc != ZUZU_OK) {
        HandleDestroy(k->space);
        return rc;
    }
    k->task = CreateTask(k->space);
    if (k->task < 0) {
        HandleDestroy(k->space);
        return (Err)k->task;
    }
    return ZUZU_OK;
}

static Err KittenCreate(Kitten *k, const uint32_t *code, size_t bytes)
{
    return KittenCreateEx(k, code, bytes, false);
}

static Err KittenStart(Kitten *k, uint32_t r0, uint32_t r1)
{
    return TaskStart(k->task, (void *)USER_ELF_BASE, (void *)USR_SP, r0, r1);
}

static void KittenFree(Kitten *k)
{
    if (k->space >= 0)
        HandleDestroy(k->space); /* tears the Space down, killing its tasks */
    if (k->task >= 0)
        HandleClose(k->task);
    k->space = k->task = -1;
}

/* ---------------- 1. svc basics ---------------- */

static void TestBasics(void)
{
    BeginSection("svc basics");

    CheckEq(ArchInvokeSvc(SVC_YIELD, 0, 0, 0, 0), ZUZU_OK, "Yield returns OK");

    /* Best of three: other kittens may still be starting up and can steal a
     * slice from any single sample, but not from all of them. */
    uint64_t best = UINT64_MAX;
    Err sleep_rc = ZUZU_OK;
    for (int i = 0; i < 3; i++) {
        uint64_t t0 = NowMs();
        sleep_rc |= Sleep(50);
        uint64_t dt = NowMs() - t0;
        if (dt < best)
            best = dt;
    }
    CheckEq(sleep_rc, ZUZU_OK, "Sleep(50) returns OK");
    if (!(best + TickMs() >= 50 && best <= 50 + 2 * TickMs()))
        UserspaceDebugLog("zztest: best Sleep(50) took %u ms (tick = %u ms)", (unsigned)best,
                          (unsigned)TickMs());
    Check(best + TickMs() >= 50 && best <= 50 + 2 * TickMs(),
          "Sleep(50) sleeps 50ms (within a tick below, two ticks above)");
    CheckEq(Sleep(0), ZUZU_OK, "Sleep(0) returns OK");

    CheckEq(ArchInvokeSvc(SVC_TOTAL_COUNT, 0, 0, 0, 0), ERR_NOSYS,
            "svc number == SVC_TOTAL_COUNT -> NOSYS");
    CheckEq(ArchInvokeSvc(0xFF, 0, 0, 0, 0), ERR_NOSYS, "svc number 0xFF -> NOSYS");
#ifdef DEBUG
    CheckEq(ArchInvokeSvc(SVC_LOG, 0, 8, 0, 0), ERR_BADPTR, "Log(NULL, 8) -> BADPTR");
    CheckEq(ArchInvokeSvc(SVC_LOG, (Register) "x", 0, 0, 0), ERR_BADPTR, "Log(ptr, 0) -> BADPTR");
    CheckEq(ArchInvokeSvc(SVC_LOG, (Register)0xC0000000u, 8, 0, 0), ERR_BADPTR,
            "Log(kernel ptr) -> BADPTR");
#endif

    const Syspage *sp = (const Syspage *)SYSPAGE;
    CheckEq((int32_t)sp->magic, (int32_t)0x50050CA7, "syspage magic");
    Check(sp->tick_hz > 0, "syspage tick_hz is set");
    Check(sp->mem_total_kb > 0 && sp->mem_free_kb <= sp->mem_total_kb,
          "syspage memory totals are sane");
    uint64_t u0 = sp->uptime_ticks;
    Sleep(20);
    Check(sp->uptime_ticks > u0, "syspage uptime advances");
    CheckEq(MemUnmap((void *)USER_BOOTINFO_VA), ERR_NOENT,
            "BootInfo page is not mapped into ordinary Spaces");
    CheckEq(MemUnmap((void *)USER_SYSPAGE_VA), ERR_NOPERM, "syspage region is pinned");
}

/* ---------------- 2. handles ---------------- */

static void TestHandles(void)
{
    BeginSection("handles");

    CheckEq(HandleClose(-1), ERR_BADHANDLE, "Close(-1) -> BADHANDLE");
    CheckEq(HandleClose(5000), ERR_BADHANDLE, "Close(out of range) -> BADHANDLE");
    CheckEq(HandleQuery(-1, QUERY_TYPE).r0, ERR_BADHANDLE, "Query(-1) -> BADHANDLE");

    Handle ev = CreateEvent();
    Handle port = CreatePort();
    Handle mem = CreateMem(1);
    Check(ev >= 0 && port >= 0 && mem >= 0, "CreateEvent/Port/Mem succeed");
    Check(ev != port && port != mem, "distinct handles");

    SvcResult q = HandleQuery(ev, QUERY_TYPE);
    CheckEq(q.r0, ZUZU_OK, "Query(type) OK");
    CheckEq(q.r1, ZH_EVENT, "event handle reports type EVENT");
    CheckEq(HandleQuery(port, QUERY_TYPE).r1, ZH_PORT, "port handle reports type PORT");
    CheckEq(HandleQuery(mem, QUERY_TYPE).r1, ZH_MEM, "mem handle reports type MEM");
    CheckEq(HandleQuery(ev, QUERY_PERMS).r1, PERM_ALL, "fresh handle has PERM_ALL");
    CheckEq(HandleQuery(ev, QUERY_MARKER).r1, MARKER_NONE, "fresh handle has no marker");
    CheckEq(HandleQuery(ev, QUERY_WHAT_COUNT).r0, ERR_BADARG, "Query(invalid what) -> BADARG");
    CheckEq(HandleQuery(ev, QUERY_STATUS).r0, ERR_BADTYPE, "Query(status) on an event -> BADTYPE");

    CheckEq(ArchInvokeSvc(SVC_MANAGEHANDLE, ev, MNGHNDL_VERB_COUNT, 0, 0), ERR_BADARG,
            "invalid ManageHandle verb -> BADARG");
    CheckEq(ArchInvokeSvc(SVC_MANAGEHANDLE, ev, MNGHNDL_IRQ_REARM, 0, 0), ERR_BADTYPE,
            "IrqRearm on a non-device -> BADTYPE");

    /* Duplicate masks permissions; it can only narrow. */
    SvcResult dup = HandleDuplicate(ev, PERM_SEND | PERM_WAIT, MARKER_NONE);
    CheckEq(dup.r0, ZUZU_OK, "Duplicate OK");
    CheckEq(HandleQuery((Handle)dup.r1, QUERY_PERMS).r1, PERM_SEND | PERM_WAIT,
            "dup carries the requested perms");
    SvcResult dup2 = HandleDuplicate((Handle)dup.r1, PERM_ALL, MARKER_NONE);
    CheckEq(HandleQuery((Handle)dup2.r1, QUERY_PERMS).r1, PERM_SEND | PERM_WAIT,
            "dup of a dup cannot widen perms");
    CheckEq(HandleRestrict((Handle)dup2.r1, PERM_WAIT), ZUZU_OK, "Restrict OK");
    CheckEq(HandleQuery((Handle)dup2.r1, QUERY_PERMS).r1, PERM_WAIT, "Restrict narrows perms");

    /* Markers: only the creator of a port may stamp one. */
    SvcResult mk = HandleDuplicate(port, PERM_SEND, 0xBEEF);
    CheckEq(HandleQuery((Handle)mk.r1, QUERY_MARKER).r1, 0xBEEF,
            "creator stamps a marker on a port dup");
    SvcResult mk_ev = HandleDuplicate(ev, PERM_SEND, 0xBEEF);
    CheckEq(HandleQuery((Handle)mk_ev.r1, QUERY_MARKER).r1, MARKER_NONE,
            "markers are ignored on non-port dups");

    /* Closing invalidates: a stale value is rejected even after the slot is reused. */
    Handle stale = (Handle)dup.r1;
    CheckEq(HandleClose(stale), ZUZU_OK, "Close OK");
    CheckEq(HandleClose(stale), ERR_BADHANDLE, "double Close -> BADHANDLE");
    Handle reuse = CreateEvent();
    CheckEq(HandleQuery(stale, QUERY_TYPE).r0, ERR_BADHANDLE,
            "stale handle stays invalid after slot reuse (generation)");
    Check(reuse >= 0 && reuse != stale, "reused slot gets a new generation");
    HandleClose(reuse);

    CheckEq(HandleDestroy(ev), ZUZU_OK, "Destroy(event) OK");
    CheckEq(HandleQuery(ev, QUERY_TYPE).r0, ERR_BADHANDLE, "destroyed handle is gone");

    HandleClose((Handle)dup2.r1);
    HandleClose((Handle)mk.r1);
    HandleClose((Handle)mk_ev.r1);
    HandleClose(port);
    HandleClose(mem);

    /* Exhaust the table: creation must fail cleanly with NOMEM, and recover. */
    static Handle many[1100];
    int made = 0;
    Handle h;
    while (made < 1100 && (h = CreateEvent()) >= 0)
        many[made++] = h;
    Check(made > 100 && made < 1100, "handle table is bounded");
    CheckEq(CreateEvent(), ERR_NOMEM, "full handle table -> NOMEM");
    for (int i = 0; i < made; i++)
        HandleClose(many[i]);
    Handle again = CreateEvent();
    Check(again >= 0, "table recovers after the handles are closed");
    HandleClose(again);
}

/* ---------------- 3. memory ---------------- */

static void TestMemory(void)
{
    BeginSection("memory");

    uint8_t *a = MemMapAnon(8192, 0, PROT_RW);
    Check(!PtrIsErr(a) && ((uintptr_t)a % 4096) == 0, "MemMapAnon returns a page-aligned address");
    volatile uint32_t *p = (volatile uint32_t *)a;
    CheckEq((int32_t)p[0], 0, "fresh anon memory reads as zero (page 0)");
    CheckEq((int32_t)p[1024], 0, "fresh anon memory reads as zero (page 1)");
    p[0] = 0xA5A5A5A5u;
    p[1024] = 0x5A5A5A5Au;
    CheckEq((int32_t)p[0], (int32_t)0xA5A5A5A5u, "anon memory is writable (page 0)");
    CheckEq((int32_t)p[1024], (int32_t)0x5A5A5A5Au, "anon memory is writable (page 1)");

    CheckEq(PtrErr(MemMapAnon(0, 0, PROT_RW)), ERR_BADARG, "size 0 -> BADARG");
    CheckEq(PtrErr(MemMapAnon(4097, 0, PROT_RW)), ERR_BADARG, "unaligned size -> BADARG");
    CheckEq(PtrErr(MemMapAnon(33u * 1024 * 1024, 0, PROT_RW)), ERR_OVERFLOW,
            "over the 32MB cap -> OVERFLOW");

    uint8_t *fixed = MemMapAnon(4096, 0x30000000u, PROT_RW);
    CheckEq((int32_t)fixed, 0x30000000, "a free page-aligned hint is honored");
    CheckEq(PtrErr(MemMapAnon(4096, 0x30000000u, PROT_RW)), ERR_NOMEM,
            "mapping over an existing region -> NOMEM");
    CheckEq(PtrErr(MemMapAnon(4096, 0x90000000u, PROT_RW)), ERR_BADARG,
            "hint above the user VA top -> BADARG");

    CheckEq(MemUnmap(fixed), ZUZU_OK, "Unmap(region start) OK");
    CheckEq(MemUnmap(fixed), ERR_NOENT, "Unmap twice -> NOENT");
    CheckEq(MemUnmap(a + 4096), ERR_BADARG, "Unmap(inside a region) -> BADARG");
    CheckEq(MemUnmap((void *)0x7000000u), ERR_NOENT, "Unmap(unmapped address) -> NOENT");

    CheckEq(MemProtect((VirtAddr)a, 8192, PROT_READ), ZUZU_OK, "Protect(read-only) OK");
    CheckEq(MemProtect((VirtAddr)a, 8192, PROT_RW), ZUZU_OK, "Protect(read-write) OK");
    CheckEq(MemProtect((VirtAddr)a, 8192, PROT_READ | PROT_EXEC), ZUZU_OK, "Protect(read+exec) OK");
    CheckEq(MemProtect((VirtAddr)a, 8192, PROT_WRITE | PROT_EXEC), ERR_BADARG, "W+X is refused");
    CheckEq(MemProtect((VirtAddr)a, 8192, (MemProt)8), ERR_BADARG, "unknown prot bits are refused");
    CheckEq(MemProtect((VirtAddr)a, 0, PROT_READ), ERR_BADARG, "Protect(size 0) -> BADARG");
    CheckEq(MemProtect((VirtAddr)a + 1, 4096, PROT_READ), ERR_BADARG,
            "Protect(unaligned va) -> BADARG");
    CheckEq(MemProtect((VirtAddr)a, 100, PROT_READ), ERR_BADARG,
            "Protect(unaligned size) -> BADARG");
    CheckEq(MemProtect(0xC0000000u, 4096, PROT_READ), ERR_BADARG,
            "Protect(kernel range) -> BADARG");
    MemProtect((VirtAddr)a, 8192, PROT_RW);
    CheckEq(MemUnmap(a), ZUZU_OK, "Unmap(anon) OK");

    /* Shared memory objects */
    Handle mem = CreateMem(2);
    Check(mem >= 0, "CreateMem(2) OK");
    uint8_t *m = MemMap(mem, 0, PROT_RW);
    Check(!PtrIsErr(m) && ((uintptr_t)m % 4096) == 0, "MemMap(mem) OK");
    memset((void *)m, 0x77, 8192);
    CheckEq(((volatile uint8_t *)m)[8191], 0x77, "shared memory spans both pages");
    CheckEq(PtrErr(MemMap(mem, 0, PROT_RW)), ERR_BUSY, "mapping a handle twice -> BUSY");

    SvcResult nomap = HandleDuplicate(mem, PERM_WAIT, MARKER_NONE);
    CheckEq(PtrErr(MemMap((Handle)nomap.r1, 0, PROT_RW)), ERR_NOPERM,
            "MemMap without PERM_MAP -> NOPERM");
    HandleClose((Handle)nomap.r1);

    CheckEq(PtrErr(MemMap(5000, 0, PROT_RW)), ERR_BADHANDLE, "MemMap(invalid handle) -> BADHANDLE");
    Handle ev = CreateEvent();
    CheckEq(PtrErr(MemMap(ev, 0, PROT_RW)), ERR_BADTYPE, "MemMap(event) -> BADTYPE");
    HandleClose(ev);

    CheckEq(HandleClose(mem), ZUZU_OK, "Close(mapped mem handle) OK");
    CheckEq(MemUnmap(m), ERR_NOENT, "closing the handle unmapped the region");

    CheckEq(CreateMem(0), ERR_BADARG, "CreateMem(0) -> BADARG");
    CheckEq(CreateMem(100000), ERR_NOMEM, "CreateMem(more than RAM) -> NOMEM");

    /* No page leaks across map/touch/unmap. One warm-up round absorbs lazy
     * one-time kernel allocations before the baseline is taken. */
    for (int round = 0; round < 2; round++) {
        uint8_t *w = MemMapAnon(16384, 0, PROT_RW);
        memset((void *)w, 1, 16384);
        MemUnmap(w);
        Handle sm = CreateMem(4);
        uint8_t *sv = MemMap(sm, 0, PROT_RW);
        memset((void *)sv, 2, 16384);
        HandleClose(sm);
    }
    SleepAndSettle();
    uint32_t before = PagesFree();
    for (int i = 0; i < 40; i++) {
        uint8_t *w = MemMapAnon(16384, 0, PROT_RW);
        memset((void *)w, 1, 16384);
        MemUnmap(w);
        Handle sm = CreateMem(4);
        uint8_t *sv = MemMap(sm, 0, PROT_RW);
        memset((void *)sv, 2, 16384);
        HandleClose(sm);
    }
    SleepAndSettle();
    CheckEq((int32_t)PagesFree(), (int32_t)before,
            "no free-page change after 40 map/touch/unmap rounds");
}

/* ---------------- 4. events ---------------- */

typedef struct {
    Handle ev;
    uint32_t bits;
    Duration delay;
    bool broadcast;
} SignalJob;

static int32_t SignalLater(void *p)
{
    SignalJob *j = p;
    Sleep(j->delay);
    return Signal(j->ev, j->bits, j->broadcast);
}

typedef struct {
    Handle ev;
    EventWord got;
    Err status;
    volatile int done;
} WaitJob;

static int32_t WaitForEvent(void *p)
{
    WaitJob *j = p;
    EventWaitResult r = FormatToEventWait(WaitOn(j->ev, 2000));
    j->status = r.status;
    j->got = r.bits;
    j->done = 1;
    return 0;
}

static void TestEvents(void)
{
    BeginSection("events");

    Handle ev = CreateEvent();
    EventWaitResult r = FormatToEventWait(WaitOn(ev, TIMEOUT_POLL));
    CheckEq(r.status, ERR_TIMEOUT, "poll on an empty event -> TIMEOUT");

    CheckEq(Signal(ev, 0x5, false), ZUZU_OK, "Signal(0x5) OK");
    CheckEq(Signal(ev, 0x2, false), ZUZU_OK, "Signal(0x2) OK (accumulates)");
    r = FormatToEventWait(WaitOn(ev, TIMEOUT_POLL));
    CheckEq(r.status, ZUZU_OK, "poll after Signal succeeds");
    CheckEq((int32_t)r.bits, 0x7, "Wait returns the OR of the signalled bits");
    r = FormatToEventWait(WaitOn(ev, TIMEOUT_POLL));
    CheckEq(r.status, ERR_TIMEOUT, "bits are cleared on delivery");

    CheckEq(Signal(ev, 0, false), ZUZU_OK, "Signal(0) is accepted");
    CheckEq(Signal(ev, 1u << 31, false), ERR_BADARG, "Signal(bit 31) -> BADARG");
    CheckEq(ArchInvokeSvc(SVC_SIGNAL, ev, 1, 2, 0), ERR_BADARG, "unknown Signal flags -> BADARG");
    CheckEq(Signal(-1, 1, false), ERR_BADHANDLE, "Signal(invalid) -> BADHANDLE");

    Handle port = CreatePort();
    CheckEq(Signal(port, 1, false), ERR_BADTYPE, "Signal(port) -> BADTYPE");
    HandleClose(port);

    SvcResult wait_only = HandleDuplicate(ev, PERM_WAIT, MARKER_NONE);
    SvcResult send_only = HandleDuplicate(ev, PERM_SEND, MARKER_NONE);
    CheckEq(Signal((Handle)wait_only.r1, 1, false), ERR_NOPERM,
            "Signal without PERM_SEND -> NOPERM");
    CheckEq(WaitOn((Handle)send_only.r1, TIMEOUT_POLL).r0, ERR_NOPERM,
            "Wait without PERM_WAIT -> NOPERM");

    uint64_t t0 = NowMs();
    r = FormatToEventWait(WaitOn(ev, 60));
    uint64_t dt = NowMs() - t0;
    CheckEq(r.status, ERR_TIMEOUT, "timed wait on an empty event -> TIMEOUT");
    Check(dt + TickMs() >= 60 && dt <= 60 + 3 * TickMs() + 40,
          "timed event wait lasts the timeout (within a tick below, bounded above)");

    /* A waiter in another thread is woken by Signal. */
    SignalJob job = {.ev = ev, .bits = 0x20, .delay = 30, .broadcast = false};
    Worker *sig = WorkerStart(SignalLater, &job);
    Check(sig != NULL, "signaller thread starts");
    r = FormatToEventWait(WaitOn(ev, 2000));
    CheckEq(r.status, ZUZU_OK, "blocked Wait is woken by another thread's Signal");
    CheckEq((int32_t)r.bits, 0x20, "woken with the signalled bit");
    CheckEq(WorkerJoin(sig), ZUZU_OK, "signaller exited cleanly");

    /* Signal wakes one waiter; broadcast wakes all. */
    WaitJob w1 = {.ev = ev}, w2 = {.ev = ev};
    Worker *a = WorkerStart(WaitForEvent, &w1);
    Worker *b = WorkerStart(WaitForEvent, &w2);
    Sleep(40);
    Signal(ev, 0x1, false);
    Sleep(40);
    CheckEq(w1.done + w2.done, 1, "plain Signal wakes exactly one of two waiters");
    Signal(ev, 0x2, false);
    WorkerJoin(a);
    WorkerJoin(b);

    WaitJob w3 = {.ev = ev}, w4 = {.ev = ev};
    a = WorkerStart(WaitForEvent, &w3);
    b = WorkerStart(WaitForEvent, &w4);
    Sleep(40);
    Signal(ev, 0x4, true);
    WorkerJoin(a);
    WorkerJoin(b);
    CheckEq(w3.done + w4.done, 2, "broadcast Signal wakes both waiters");
    CheckEq((int32_t)(w3.got & 0x4), 0x4, "broadcast delivers the bits to the first waiter");

    /* Destroying the last handle wakes waiters with DEAD. */
    Handle doomed = CreateEvent();
    WaitJob wd = {.ev = doomed};
    a = WorkerStart(WaitForEvent, &wd);
    Sleep(40);
    HandleClose(doomed);
    WorkerJoin(a);
    CheckEq(wd.status, ERR_DEAD, "closing the event wakes a blocked waiter with DEAD");

    /* Kernel-owned bits cannot be forged from user space. */
    Handle bp = CreatePort();
    CheckEq(Bind(EVENT_PORT, ev, bp, 3), ZUZU_OK, "Bind(port) OK");
    CheckEq(Signal(ev, 1u << 3, false), ERR_NOPERM, "Signal on a bound bit -> NOPERM");
    CheckEq(Signal(ev, 1u << 4, false), ZUZU_OK, "other bits on the same event are fine");
    r = FormatToEventWait(WaitOn(ev, TIMEOUT_POLL));
    CheckEq((int32_t)r.bits, 1 << 4, "only the unbound bit was delivered");

    CheckEq(Bind(EVENT_PORT, ev, bp, 31), ERR_BADARG, "Bind(bit 31) -> BADARG");
    CheckEq(Bind(EVENT_PORT, ev, ev, 5), ERR_BADTYPE, "Bind(port, <event handle>) -> BADTYPE");
    CheckEq(Bind(EVENT_PORT, ev, -1, 5), ERR_BADHANDLE, "Bind(invalid target) -> BADHANDLE");
    CheckEq(Bind((EventType)99, ev, bp, 5), ERR_BADARG, "Bind(unknown type) -> BADARG");
    CheckEq(Bind(EVENT_PORT, bp, bp, 5), ERR_BADTYPE, "Bind(<non-event>, ...) -> BADTYPE");
    CheckEq(Bind(EVENT_PORT, (Handle)send_only.r1, bp, 5), ERR_NOPERM,
            "Bind without PERM_CNTL on the event -> NOPERM");
    SvcResult no_wait_port = HandleDuplicate(bp, PERM_SEND, MARKER_NONE);
    CheckEq(Bind(EVENT_PORT, ev, (Handle)no_wait_port.r1, 5), ERR_NOPERM,
            "Bind without PERM_WAIT on the target -> NOPERM");
    HandleClose((Handle)no_wait_port.r1);
    CheckEq(BindIrq(ev, 5000, 0), ERR_BADHANDLE, "BindIrq(invalid device) -> BADHANDLE");
    Handle shm = CreateMem(1);
    CheckEq(BindIrq(ev, shm, 0), ERR_BADTYPE, "BindIrq(non-device memory) -> BADTYPE");
    HandleClose(shm);

    HandleClose((Handle)wait_only.r1);
    HandleClose((Handle)send_only.r1);
    HandleClose(bp);
    HandleClose(ev);
}

/* ---------------- 5. ipc ---------------- */

enum {
    OP_ECHO = 1,   /* reply the request bytes, each XOR 0xFF */
    OP_MARKER,     /* reply the sender's marker */
    OP_GRANT_INFO, /* reply {granted handle, its type, its perms}; close it */
    OP_GRANT_BACK, /* reply 4 bytes and grant the caller a send-only event dup */
    OP_DIE,        /* exit without replying */
    OP_BIG_REPLY   /* try an oversized Reply, record the rc, then reply properly */
};

typedef struct {
    Handle port;
    Handle ev; /* an event the server can grant back */
    volatile int32_t big_reply_rc;
    volatile uint32_t served;
} Server;

static int32_t RunServer(void *p)
{
    Server *s = p;
    for (;;) {
        PortWaitResult r = FormatToPortWait(WaitOn(s->port, 5000));
        if (r.status == ERR_TIMEOUT)
            continue;
        if (r.status != ZUZU_OK)
            return r.status; /* DEAD once the test closes the port */
        s->served++;

        uint8_t req[MSG_BUF_SIZE];
        uint32_t len = r.xlen > MSG_BUF_SIZE ? MSG_BUF_SIZE : r.xlen;
        memcpy(req, GetMessageBox(), len);
        uint32_t op = 0;
        if (len >= 4)
            memcpy(&op, req, 4);

        switch (op) {
        case OP_ECHO: {
            uint8_t *out = GetMessageBox();
            for (uint32_t i = 4; i < len; i++)
                out[i - 4] = (uint8_t)(req[i] ^ 0xFF);
            Reply(len > 4 ? len - 4 : 0, -1);
        } break;
        case OP_MARKER: {
            uint32_t m = (uint32_t)r.sender;
            memcpy(GetMessageBox(), &m, 4);
            Reply(4, -1);
        } break;
        case OP_GRANT_INFO: {
            uint32_t info[3] = {(uint32_t)r.granted, 0, 0};
            if (r.granted >= 0) {
                info[1] = (uint32_t)HandleQuery(r.granted, QUERY_TYPE).r1;
                info[2] = (uint32_t)HandleQuery(r.granted, QUERY_PERMS).r1;
                HandleClose(r.granted);
            }
            memcpy(GetMessageBox(), info, sizeof(info));
            Reply(sizeof(info), -1);
        } break;
        case OP_GRANT_BACK: {
            SvcResult d = HandleDuplicate(s->ev, PERM_SEND | PERM_TXFR, MARKER_NONE);
            memcpy(GetMessageBox(), "ok!!", 4);
            Reply(4, (Handle)d.r1);
            HandleClose((Handle)d.r1);
        } break;
        case OP_DIE:
            return 99;
        case OP_BIG_REPLY:
            s->big_reply_rc = Reply(MSG_BUF_SIZE + 1, -1);
            Reply(0, -1);
            break;
        default:
            if (r.granted >= 0)
                HandleClose(r.granted);
            memcpy(GetMessageBox(), "????", 4);
            Reply(4, -1);
            break;
        }
    }
}

static SvcResult Rpc(Handle port, uint32_t op, const void *body, uint32_t blen, Handle grant)
{
    uint8_t *buf = GetMessageBox();
    memcpy(buf, &op, 4);
    if (blen)
        memcpy(buf + 4, body, blen);
    return Call(port, 4 + blen, grant);
}

typedef struct {
    Handle port;
    uint32_t op;
} CallerJob;

static int32_t CallOnce(void *p)
{
    CallerJob *j = p;
    SvcResult r = Rpc(j->port, j->op, NULL, 0, -1);
    return (Err)r.r0;
}

static void TestIpc(void)
{
    BeginSection("ipc");

    Server srv = {.port = CreatePort(), .ev = CreateEvent()};
    Worker *sw = WorkerStart(RunServer, &srv);
    Check(srv.port >= 0 && sw != NULL, "server thread starts");

    uint8_t body[16];
    for (int i = 0; i < 16; i++)
        body[i] = (uint8_t)(i * 3 + 1);

    SvcResult r = Rpc(srv.port, OP_ECHO, body, sizeof(body), -1);
    CheckEq(r.r0, ZUZU_OK, "Call OK");
    CheckEq(r.r1, 16, "reply length is reported in r1");
    CheckEq(r.r3, -1, "no grant -> granted == -1");
    bool match = true;
    for (int i = 0; i < 16; i++)
        match &= ((uint8_t *)GetMessageBox())[i] == (uint8_t)(body[i] ^ 0xFF);
    Check(match, "reply payload round-trips through MessageBuf");

    r = Rpc(srv.port, OP_ECHO, NULL, 0, -1);
    CheckEq(r.r0, ZUZU_OK, "zero-length request OK");
    CheckEq(r.r1, 0, "zero-length reply");

    uint8_t big[MSG_BUF_SIZE];
    memset(big, 0x11, sizeof(big));
    memcpy(GetMessageBox(), big, sizeof(big));
    uint32_t op = OP_ECHO;
    memcpy(GetMessageBox(), &op, 4);
    r = Call(srv.port, MSG_BUF_SIZE, -1);
    CheckEq(r.r0, ZUZU_OK, "maximum-size request OK");
    CheckEq(r.r1, MSG_BUF_SIZE - 4, "maximum-size reply length");
    CheckEq(Call(srv.port, MSG_BUF_SIZE + 1, -1).r0, ERR_OVERFLOW,
            "request over MSG_BUF_SIZE -> OVERFLOW");

    CheckEq(Call(-1, 4, -1).r0, ERR_BADHANDLE, "Call(invalid) -> BADHANDLE");
    Handle ev = CreateEvent();
    CheckEq(Call(ev, 4, -1).r0, ERR_BADTYPE, "Call(event) -> BADTYPE");
    HandleClose(ev);
    SvcResult no_send = HandleDuplicate(srv.port, PERM_WAIT, MARKER_NONE);
    CheckEq(Call((Handle)no_send.r1, 4, -1).r0, ERR_NOPERM, "Call without PERM_SEND -> NOPERM");

    /* Markers */
    SvcResult marked = HandleDuplicate(srv.port, PERM_SEND, 0xBEEF);
    r = Rpc((Handle)marked.r1, OP_MARKER, NULL, 0, -1);
    uint32_t seen = 0;
    memcpy(&seen, GetMessageBox(), 4);
    CheckEq((int32_t)seen, 0xBEEF, "receiver sees the sender's marker");
    r = Rpc(srv.port, OP_MARKER, NULL, 0, -1);
    memcpy(&seen, GetMessageBox(), 4);
    CheckEq((int32_t)seen, MARKER_NONE, "unmarked caller has marker 0");
    HandleClose((Handle)marked.r1);
    HandleClose((Handle)no_send.r1);

    /* Grants: perms copy verbatim and PERM_TXFR is required. */
    Handle gev = CreateEvent();
    SvcResult no_txfr = HandleDuplicate(gev, PERM_SEND, MARKER_NONE);
    CheckEq(Rpc(srv.port, OP_GRANT_INFO, NULL, 0, (Handle)no_txfr.r1).r0, ERR_NOPERM,
            "granting a handle without TXFR -> NOPERM");
    HandleClose((Handle)no_txfr.r1);
    SvcResult gd = HandleDuplicate(gev, PERM_SEND | PERM_TXFR, MARKER_NONE);
    r = Rpc(srv.port, OP_GRANT_INFO, NULL, 0, (Handle)gd.r1);
    CheckEq(r.r0, ZUZU_OK, "Call with a grant OK");
    uint32_t info[3];
    memcpy(info, GetMessageBox(), sizeof(info));
    Check((int32_t)info[0] >= 0, "receiver got a handle");
    CheckEq((int32_t)info[1], ZH_EVENT, "granted handle keeps its type");
    CheckEq((int32_t)info[2], PERM_SEND | PERM_TXFR, "granted handle keeps its perms verbatim");
    CheckEq(HandleQuery((Handle)gd.r1, QUERY_TYPE).r0, ZUZU_OK, "the sender still owns its handle");
    CheckEq(Call(srv.port, 4, 5000).r0, ERR_BADHANDLE, "granting an invalid handle -> BADHANDLE");
    HandleClose((Handle)gd.r1);
    HandleClose(gev);

    r = Rpc(srv.port, OP_GRANT_BACK, NULL, 0, -1);
    CheckEq(r.r0, ZUZU_OK, "Reply with a grant OK");
    Check(r.r3 >= 0, "caller receives the granted handle in r3");
    CheckEq(HandleQuery((Handle)r.r3, QUERY_PERMS).r1, PERM_SEND | PERM_TXFR,
            "reply-granted perms are verbatim");
    CheckEq(Signal((Handle)r.r3, 1, false), ZUZU_OK, "the granted handle works");
    WaitOn(srv.ev, TIMEOUT_POLL);
    HandleClose((Handle)r.r3);

    srv.big_reply_rc = 0;
    r = Rpc(srv.port, OP_BIG_REPLY, NULL, 0, -1);
    CheckEq(r.r0, ZUZU_OK, "caller still gets its reply after a refused oversized Reply");
    CheckEq(srv.big_reply_rc, ERR_OVERFLOW, "Reply over MSG_BUF_SIZE -> OVERFLOW");

    CheckEq(Reply(0, -1), ERR_BADHANDLE, "Reply with no call outstanding -> BADHANDLE");
    uint32_t served = srv.served;
    Check(served >= 8, "server counted the calls");

    /* A server that dies mid-call fails its caller with DEAD. */
    Server doomed = {.port = CreatePort(), .ev = -1};
    Worker *dw = WorkerStart(RunServer, &doomed);
    CheckEq(Rpc(doomed.port, OP_DIE, NULL, 0, -1).r0, ERR_DEAD,
            "server exiting mid-call -> caller gets DEAD");
    CheckEq(WorkerJoin(dw), 99, "dying server exit status");
    HandleClose(doomed.port);

    /* Closing the port fails queued callers and blocked receivers. */
    Handle lone = CreatePort();
    CallerJob cj = {.port = lone, .op = OP_ECHO};
    Worker *cw = WorkerStart(CallOnce, &cj);
    Sleep(40);
    HandleClose(lone);
    CheckEq(WorkerJoin(cw), ERR_DEAD, "closing a port fails a queued caller with DEAD");

    Server idle = {.port = CreatePort(), .ev = -1};
    Worker *iw = WorkerStart(RunServer, &idle);
    Sleep(40);
    HandleClose(idle.port);
    CheckEq(WorkerJoin(iw), ERR_DEAD, "closing a port fails a blocked receiver with DEAD");

    /* Receiving while a call is outstanding is refused; ReplyRecv chains. */
    Handle p2 = CreatePort();
    CallerJob c1 = {.port = p2, .op = OP_ECHO};
    Worker *c1w = WorkerStart(CallOnce, &c1);
    PortWaitResult got = FormatToPortWait(WaitOn(p2, 2000));
    CheckEq(got.status, ZUZU_OK, "WaitOn(port) receives the call");
    CheckEq(WaitOn(p2, TIMEOUT_POLL).r0, ERR_BUSY,
            "second receive with an unanswered call -> BUSY");
    CallerJob c2 = {.port = p2, .op = OP_ECHO};
    Worker *c2w = WorkerStart(CallOnce, &c2);
    Sleep(40);
    SvcResult rr = ReplyRecv(0, -1, p2, 2000);
    CheckEq(rr.r0, ZUZU_OK, "ReplyRecv replies and receives the next call");
    CheckEq(WorkerJoin(c1w), ZUZU_OK, "first caller completed");
    Reply(0, -1);
    CheckEq(WorkerJoin(c2w), ZUZU_OK, "second caller completed");
    CheckEq(WaitOn(p2, TIMEOUT_POLL).r0, ERR_TIMEOUT, "poll on an idle port -> TIMEOUT");
    uint64_t t0 = NowMs();
    CheckEq(WaitOn(p2, 50).r0, ERR_TIMEOUT, "timed wait on an idle port -> TIMEOUT");
    Check(NowMs() - t0 + TickMs() >= 50,
          "port timed wait lasts at least the timeout (minus a tick)");

    /* A bound port raises its event bit when a caller is queued. */
    Handle pev = CreateEvent();
    CheckEq(Bind(EVENT_PORT, pev, p2, 2), ZUZU_OK, "Bind(port, bit 2) OK");
    CallerJob c3 = {.port = p2, .op = OP_ECHO};
    Worker *c3w = WorkerStart(CallOnce, &c3);
    EventWaitResult er = FormatToEventWait(WaitOn(pev, 2000));
    CheckEq(er.status, ZUZU_OK, "queued caller wakes the bound event");
    CheckEq((int32_t)(er.bits & (1u << 2)), 1 << 2, "the bound bit is the one raised");
    got = FormatToPortWait(WaitOn(p2, TIMEOUT_POLL));
    CheckEq(got.status, ZUZU_OK, "the queued call is there to receive");
    Reply(0, -1);
    CheckEq(WorkerJoin(c3w), ZUZU_OK, "bound-port caller completed");
    HandleClose(pev);
    HandleClose(p2);

    CheckEq(WaitOn(-1, 0).r0, ERR_BADHANDLE, "WaitOn(invalid) -> BADHANDLE");
    Handle mem = CreateMem(1);
    CheckEq(WaitOn(mem, 0).r0, ERR_BADTYPE, "WaitOn(mem) -> BADTYPE");
    HandleClose(mem);

    HandleClose(srv.port);
    WorkerJoin(sw);
    HandleClose(srv.ev);
}

/* ---------------- 4b. multi-observer bindings ---------------- */

static int32_t SleepThenExit3(void *p)
{
    Sleep((Duration)(uintptr_t)p);
    return 3;
}

static EventWord PollBits(Handle ev)
{
    EventWaitResult r = FormatToEventWait(WaitOn(ev, 1000));
    return r.status == ZUZU_OK ? r.bits : 0;
}

static void TestObservers(void)
{
    BeginSection("multi-observer binds");

    Handle e[6];
    for (int i = 0; i < 6; i++)
        e[i] = CreateEvent();

    /* Several events on one task; a retarget; a full table. */
    Worker *w = WorkerStart(SleepThenExit3, (void *)(uintptr_t)120);
    CheckEq(Bind(EVENT_TASK, e[0], w->task, 2), ZUZU_OK, "first observer on a task");
    CheckEq(Bind(EVENT_TASK, e[1], w->task, 5), ZUZU_OK, "second observer on the same task");
    CheckEq(Bind(EVENT_TASK, e[0], w->task, 3), ZUZU_OK,
            "re-binding the same event just moves its bit");
    CheckEq(Bind(EVENT_TASK, e[2], w->task, 1), ZUZU_OK, "third observer");
    CheckEq(Bind(EVENT_TASK, e[3], w->task, 4), ZUZU_OK, "fourth observer");
    CheckEq(Bind(EVENT_TASK, e[4], w->task, 6), ERR_NOMEM,
            "a fifth distinct event -> NOMEM (table full)");
    CheckEq(WaitOn(e[4], TIMEOUT_POLL).r0, ERR_TIMEOUT, "the rejected event was never attached");
    CheckEq(WaitOn(w->task, 20).r0, ERR_TIMEOUT, "(the task is still running)");
    CheckEq((int32_t)PollBits(e[0]), 1 << 3, "observer 1 fires on the retargeted bit only");
    CheckEq((int32_t)PollBits(e[1]), 1 << 5, "observer 2 fires");
    CheckEq((int32_t)PollBits(e[2]), 1 << 1, "observer 3 fires");
    CheckEq((int32_t)PollBits(e[3]), 1 << 4, "observer 4 fires");
    CheckEq(WorkerJoin(w), 3, "the observed task exited with its status");

    /* A bind on a task that is already dead signals only the new observer. */
    Worker *d = WorkerStart(SleepThenExit3, (void *)(uintptr_t)0);
    CheckEq(Bind(EVENT_TASK, e[0], d->task, 0), ZUZU_OK, "observer attached before the exit");
    CheckEq((int32_t)PollBits(e[0]), 1 << 0, "and it fired");
    CheckEq(Bind(EVENT_TASK, e[5], d->task, 7), ZUZU_OK, "late bind on a dead task OK");
    CheckEq((int32_t)PollBits(e[5]), 1 << 7, "late observer is signalled immediately");
    CheckEq(WaitOn(e[0], TIMEOUT_POLL).r0, ERR_TIMEOUT, "earlier observers are not notified again");
    WorkerJoin(d);

    /* Ports */
    Handle port = CreatePort();
    CheckEq(Bind(EVENT_PORT, e[0], port, 1), ZUZU_OK, "observer 1 on a port");
    CheckEq(Bind(EVENT_PORT, e[1], port, 2), ZUZU_OK, "observer 2 on the same port");
    CallerJob cj = {.port = port, .op = OP_ECHO};
    Worker *cw = WorkerStart(CallOnce, &cj);
    CheckEq((int32_t)(PollBits(e[0]) & (1u << 1)), 1 << 1,
            "a queued caller fires the first port observer");
    CheckEq((int32_t)(PollBits(e[1]) & (1u << 2)), 1 << 2, "and the second");
    FormatToPortWait(WaitOn(port, 1000));
    Reply(0, -1);
    WorkerJoin(cw);
    CheckEq(Bind(EVENT_PORT, e[2], port, 3), ZUZU_OK, "third port observer");
    HandleClose(port);

    /* Spaces */
    Kitten k;
    KittenCreate(&k, kCodeQuit42, sizeof(kCodeQuit42));
    CheckEq(Bind(EVENT_SPACE, e[3], k.space, 4), ZUZU_OK, "observer 1 on a space");
    CheckEq(Bind(EVENT_SPACE, e[4], k.space, 5), ZUZU_OK, "observer 2 on the same space");
    KittenStart(&k, 0, 0);
    CheckEq((int32_t)PollBits(e[3]), 1 << 4, "the space going hollow fires the first observer");
    CheckEq((int32_t)(PollBits(e[4]) & (1u << 5)), 1 << 5, "and the second");
    KittenFree(&k);

    for (int i = 0; i < 6; i++)
        HandleClose(e[i]);
}

/* ---------------- 6. tasks (same Space) ---------------- */

static atomic_int g_flag;

static int32_t ExitWith42(void *p)
{
    (void)p;
    return 42;
}

static int32_t SleepForever(void *p)
{
    (void)p;
    for (;;)
        Sleep(1000);
    return 0;
}

static int32_t ArgToExit(void *p) { return (int32_t)(uintptr_t)p; }

static int32_t PriorityWorker(void *p)
{
    (void)p;
    int bad = 0;
    bad += TaskSetPriority(-1, 3) != ZUZU_OK;
    bad += TaskSetPriority(-1, 7) != ZUZU_OK;
    bad += TaskSetPriority(-1, 8) != ERR_NOPERM;
    bad += TaskSetMaxPriority(-1, 4) != ZUZU_OK;
    bad += TaskSetPriority(-1, 5) != ERR_NOPERM;
    bad += TaskSetMaxPriority(-1, 6) != ERR_NOPERM;
    bad += TaskSetPriority(-1, 4) != ZUZU_OK;
    return bad;
}

static void TestTasks(void)
{
    BeginSection("tasks");

    Worker *w = WorkerStart(ExitWith42, NULL);
    Check(w != NULL, "CreateTask + TaskStart OK");
    TaskWaitResult tw = FormatToTaskWait(WaitOn(w->task, 2000));
    CheckEq(tw.status, ZUZU_OK, "WaitOn(task) returns once the task exits");
    CheckEq(tw.outcome, TASK_EXITED, "outcome is EXITED");
    CheckEq(tw.value, 42, "exit status is carried through");
    tw = FormatToTaskWait(WaitOn(w->task, TIMEOUT_POLL));
    CheckEq(tw.value, 42, "waiting on an already-dead task returns immediately");
    CheckEq(HandleQuery(w->task, QUERY_STATUS).r1, 42, "Query(status) reports the exit status");
    CheckEq(HandleQuery(w->task, QUERY_TYPE).r1, ZH_TASK, "task handle reports type TASK");
    /* Regression: killing a task that already exited used to TaskDestroy it a
     * second time, and the last handle close then freed it while it was still
     * queued for reaping (SchedConsumeDestroyQueue faulted). */
    CheckEq(TaskKill(w->task), ZUZU_OK, "killing a dead task is harmless");
    CheckEq(TaskKill(w->task), ZUZU_OK, "killing a dead task twice is harmless");
    WorkerJoin(w);

    w = WorkerStart(ArgToExit, (void *)(uintptr_t)1234);
    CheckEq(WorkerJoin(w), 1234, "TaskStart passes r0 to the entry function");

    Handle t = CreateTask(-1);
    Check(t >= 0, "CreateTask(-1) creates a task in the caller's Space");
    uint8_t *stack = MemMapAnon(WORKER_STACK, 0, PROT_RW);
    CheckEq(TaskStart(t, (void *)SleepForever, stack + WORKER_STACK, 0, 0), ZUZU_OK,
            "TaskStart OK");
    CheckEq(TaskStart(t, (void *)SleepForever, stack + WORKER_STACK, 0, 0), ERR_BUSY,
            "TaskStart on a running task -> BUSY");
    CheckEq(WaitOn(t, 40).r0, ERR_TIMEOUT, "WaitOn(task) with a timeout while it runs -> TIMEOUT");
    CheckEq(WaitOn(t, 0).r0, ERR_TIMEOUT, "polling a running task -> TIMEOUT");

    Handle ev = CreateEvent();
    CheckEq(Bind(EVENT_TASK, ev, t, 5), ZUZU_OK, "Bind(task) OK");
    CheckEq(TaskKill(t), ZUZU_OK, "TaskKill OK");
    EventWaitResult er = FormatToEventWait(WaitOn(ev, 1000));
    CheckEq(er.status, ZUZU_OK, "task exit raises the bound event");
    CheckEq((int32_t)er.bits, 1 << 5, "the bound task bit");
    tw = FormatToTaskWait(WaitOn(t, TIMEOUT_POLL));
    CheckEq(tw.outcome, TASK_EXITED, "killed task reports EXITED");
    CheckEq(tw.value, ERR_DEAD, "killed task's status is DEAD");
    HandleClose(ev);
    HandleClose(t);
    MemUnmap(stack);

    CheckEq(TaskKill(-1), ERR_BADARG, "TaskKill(-1) is not a thing");
    Handle me = CreateEvent();
    CheckEq(TaskKill(me), ERR_BADTYPE, "TaskKill(event) -> BADTYPE");
    CheckEq(TaskKill(5000), ERR_BADHANDLE, "TaskKill(invalid) -> BADHANDLE");
    CheckEq(TaskStart(me, 0, 0, 0, 0), ERR_BADTYPE, "TaskStart(event) -> BADTYPE");
    SvcResult no_cntl = HandleDuplicate(me, PERM_WAIT, MARKER_NONE);
    HandleClose((Handle)no_cntl.r1);
    HandleClose(me);

    Handle self_task = CreateTask(-1);
    SvcResult no_ctl = HandleDuplicate(self_task, PERM_WAIT, MARKER_NONE);
    CheckEq(TaskKill((Handle)no_ctl.r1), ERR_NOPERM, "TaskKill without PERM_CNTL -> NOPERM");
    CheckEq(WaitOn(self_task, 0).r0, ERR_TIMEOUT, "an unstarted (frozen) task is not dead");
    CheckEq(TaskResume(self_task), ERR_BADARG, "Resume on a task that has not faulted -> BADARG");
    CheckEq(TaskSuspend(self_task), ERR_NOSYS, "Suspend is not implemented -> NOSYS");
    CheckEq(TaskSetTimeSlice(self_task, 3), ZUZU_OK, "SetTimeSlice OK");
    CheckEq(TaskSetPriority(self_task, 2), ZUZU_OK, "SetPriority on another task OK");
    CheckEq(TaskSetPriority(self_task, 9), ERR_NOPERM, "SetPriority above max -> NOPERM");
    CheckEq(ArchInvokeSvc(SVC_MANAGETASK, self_task, MNGTASK_VERB_COUNT, 0, 0), ERR_BADARG,
            "invalid ManageTask verb -> BADARG");
    CheckEq(ArchInvokeSvc(SVC_MANAGETASK, -1, MNGTASK_KILL, 0, 0), ERR_BADARG,
            "task-less verbs are limited to priority/slice");
    HandleClose((Handle)no_ctl.r1);
    HandleClose(self_task);

    Worker *pw = WorkerStart(PriorityWorker, NULL);
    CheckEq(WorkerJoin(pw), 0, "priority rules: can lower but never raise past max_prio");
    CheckEq(TaskSetPriority(-1, 1), ZUZU_OK, "SetPriority(-1) on the caller OK");
    CheckEq(TaskSetTimeSlice(-1, 5), ZUZU_OK, "SetTimeSlice(-1) OK");

    /* Many short-lived threads: slots and stacks are recycled. */
    int ok = 0;
    for (int i = 0; i < 20; i++) {
        Worker *s = WorkerStart(ArgToExit, (void *)(uintptr_t)i);
        if (s && WorkerJoin(s) == i)
            ok++;
    }
    CheckEq(ok, 20, "20 sequential thread create/join cycles");
    atomic_store(&g_flag, 0);
}

/* ---------------- 7. spaces, faults, MemInject ---------------- */

static void TestSpaces(void)
{
    BeginSection("spaces and faults");

    /* Normal exit */
    Kitten k;
    CheckEq(KittenCreate(&k, kCodeQuit42, sizeof(kCodeQuit42)), ZUZU_OK,
            "build a kitten that exits with 42");
    CheckEq(HandleQuery(k.space, QUERY_TYPE).r1, ZH_SPACE, "space handle reports type SPACE");
    Handle ev = CreateEvent();
    CheckEq(Bind(EVENT_SPACE, ev, k.space, 4), ZUZU_OK, "Bind(space) OK");
    CheckEq(Bind(EVENT_TASK, ev, k.task, 6), ZUZU_OK, "Bind(task) OK");
    CheckEq(WaitOn(k.space, TIMEOUT_POLL).r0, ERR_TIMEOUT,
            "a Space with an unstarted task is not hollow");
    CheckEq(KittenStart(&k, 0, 0), ZUZU_OK, "start the kitten");
    SpaceWaitResult sw = FormatToSpaceWait(WaitOn(k.space, 2000));
    CheckEq(sw.status, ZUZU_OK, "WaitOn(space) returns when the last task exits");
    CheckEq(sw.exit_status, 42, "Space exit status is the last task's");
    sw = FormatToSpaceWait(WaitOn(k.space, TIMEOUT_POLL));
    CheckEq(sw.exit_status, 42, "waiting on a hollow Space returns immediately");
    CheckEq(HandleQuery(k.space, QUERY_STATUS).r1, 42, "Query(status) on the Space");
    EventWaitResult er = FormatToEventWait(WaitOn(ev, 1000));
    CheckEq((int32_t)er.bits, (1 << 4) | (1 << 6), "task and space binds both raised their bits");
    KittenFree(&k);
    HandleClose(ev);

    /* Ancestry: a Space may not close or destroy a handle to itself or to a
     * Space that created it; a sibling is fair game. */
    const uint32_t *self_code[2] = {kCodeCloseHandle, kCodeDestroyHandle};
    for (int i = 0; i < 2; i++) {
        Kitten s;
        KittenCreate(&s, self_code[i], sizeof(kCodeCloseHandle));
        SvcResult g = HandleGrant(s.space, s.space, PERM_ALL);
        CheckEq((Err)g.r0, ZUZU_OK, "grant a Space a handle to itself");
        KittenStart(&s, (uint32_t)g.r1, 0);
        TaskWaitResult sw_self = FormatToTaskWait(WaitOn(s.task, 2000));
        CheckEq(sw_self.value, ERR_BADARG,
                i ? "a Space cannot destroy a handle to itself"
                  : "a Space cannot close a handle to itself");
        KittenFree(&s);
    }
    Kitten a, b;
    KittenCreate(&a, kCodeLoop, sizeof(kCodeLoop));
    KittenCreate(&b, kCodeCloseHandle, sizeof(kCodeCloseHandle));
    SvcResult sib = HandleGrant(a.space, b.space, PERM_ALL);
    CheckEq((Err)sib.r0, ZUZU_OK, "grant a Space a handle to its sibling");
    KittenStart(&b, (uint32_t)sib.r1, 0);
    TaskWaitResult sib_tw = FormatToTaskWait(WaitOn(b.task, 2000));
    CheckEq(sib_tw.value, ZUZU_OK, "a Space may close a handle to its sibling");
    KittenFree(&b);
    KittenFree(&a);

    /* Registers and kill */
    CheckEq(KittenCreate(&k, kCodeLoop, sizeof(kCodeLoop)), ZUZU_OK, "build a spinning kitten");
    CheckEq(KittenStart(&k, 0x1234, 0x5678), ZUZU_OK, "start it with r0/r1 arguments");
    Register regs[ARCH_NUM_GP_REGS];
    CheckEq(TaskGetRegs(k.task, regs), ZUZU_OK, "GetRegs OK");
    CheckEq(regs[0], 0x1234, "r0 holds the first start argument");
    CheckEq(regs[1], 0x5678, "r1 holds the second start argument");
    regs[0] = 0xCAFE;
    CheckEq(TaskSetRegs(k.task, regs), ZUZU_OK, "SetRegs OK");
    Register back[ARCH_NUM_GP_REGS];
    TaskGetRegs(k.task, back);
    CheckEq(back[0], 0xCAFE, "SetRegs is visible to GetRegs");
    CheckEq(WaitOn(k.space, 40).r0, ERR_TIMEOUT, "a running kitten is not hollow");
    CheckEq(TaskKill(k.task), ZUZU_OK, "TaskKill on a running task in another Space");
    sw = FormatToSpaceWait(WaitOn(k.space, 2000));
    CheckEq(sw.status, ZUZU_OK, "the killed kitten's Space becomes hollow");
    CheckEq(sw.exit_status, ERR_DEAD, "its exit status is DEAD");
    KittenFree(&k);

    /* Faults */
    struct {
        const uint32_t *code;
        size_t bytes;
        int32_t reason;
        const char *name;
    } faults[] = {
        {kCodeFaultData, sizeof(kCodeFaultData), KILLED_FAULT_DATA, "data abort"},
        {kCodeFaultPrefetch, sizeof(kCodeFaultPrefetch), KILLED_FAULT_PREFETCH, "prefetch abort"},
        {kCodeFaultUndef, sizeof(kCodeFaultUndef), KILLED_FAULT_UNDEF, "undefined instruction"},
    };
    for (unsigned i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        CheckEq(KittenCreate(&k, faults[i].code, faults[i].bytes), ZUZU_OK,
                "build a faulting kitten");
        Handle fev = CreateEvent();
        Bind(EVENT_TASK, fev, k.task, 3);
        KittenStart(&k, 0, 0);
        TaskWaitResult tw = FormatToTaskWait(WaitOn(k.task, 2000));
        CheckEq(tw.status, ZUZU_OK, faults[i].name);
        CheckEq(tw.outcome, TASK_FAULTED, "WaitOn(task) reports FAULTED");
        CheckEq(tw.value, faults[i].reason, "the fault reason identifies the exception");
        EventWaitResult fr = FormatToEventWait(WaitOn(fev, 500));
        CheckEq((int32_t)fr.bits, 1 << 3, "a fault raises the task's bound event");
        CheckEq(WaitOn(k.space, 50).r0, ERR_TIMEOUT,
                "a faulted task keeps its Space alive (frozen)");
        tw = FormatToTaskWait(WaitOn(k.task, TIMEOUT_POLL));
        CheckEq(tw.outcome, TASK_FAULTED, "waiting after the fault still reports FAULTED");
        CheckEq(TaskKill(k.task), ZUZU_OK, "TaskKill on a faulted task");
        sw = FormatToSpaceWait(WaitOn(k.space, 2000));
        CheckEq(sw.exit_status, ERR_DEAD, "killing a faulted task empties its Space");
        HandleClose(fev);
        KittenFree(&k);
    }

    /* Fix up and resume */
    CheckEq(KittenCreate(&k, kCodeLoadThenQuit7, sizeof(kCodeLoadThenQuit7)), ZUZU_OK,
            "build a resumable kitten");
    KittenStart(&k, 0, 0);
    TaskWaitResult tw = FormatToTaskWait(WaitOn(k.task, 2000));
    CheckEq(tw.outcome, TASK_FAULTED, "it faults loading from address 0");
    CheckEq(TaskGetRegs(k.task, regs), ZUZU_OK, "GetRegs on a faulted task OK");
    CheckEq(regs[0], 0, "r0 holds the bad address");
    regs[0] = USER_SYSPAGE_VA;
    CheckEq(TaskSetRegs(k.task, regs), ZUZU_OK, "patch r0 to a readable address");
    CheckEq(TaskResume(k.task), ZUZU_OK, "TaskResume OK");
    sw = FormatToSpaceWait(WaitOn(k.space, 2000));
    CheckEq(sw.status, ZUZU_OK, "the resumed kitten runs on");
    CheckEq(sw.exit_status, 7, "and exits through the instructions after the fault");
    CheckEq(TaskResume(k.task), ERR_BADARG, "Resume on a task that is no longer faulted -> BADARG");
    KittenFree(&k);

    /* MemInject contract */
    Handle sp = CreateSpace("zz-inject");
    memset(g_code_page, 0x90, sizeof(g_code_page));
    CheckEq(MemInject(sp, USER_ELF_BASE + 1, g_code_page, 4096, PROT_READ, 0), ERR_BADARG,
            "unaligned destination -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, g_code_page, 0, PROT_READ, 0), ERR_BADARG,
            "zero length -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, g_code_page, 4096, PROT_WRITE | PROT_EXEC, 0), ERR_BADARG,
            "W+X -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, g_code_page, 4096, (MemProt)8, 0), ERR_BADARG,
            "unknown prot bits -> BADARG");
    CheckEq(MemInject(sp, 0x80000000u, g_code_page, 4096, PROT_READ, 0), ERR_BADARG,
            "destination in kernel space -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, NULL, 4096, PROT_READ, 0), ERR_BADARG,
            "NULL source -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, g_code_page, 4096, PROT_READ, ASINJECT_FLAG_RESERVE),
            ERR_BADARG, "RESERVE with a source -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, NULL, 100, PROT_READ, ASINJECT_FLAG_RESERVE), ERR_BADARG,
            "RESERVE length not page-aligned -> BADARG");
    CheckEq(MemInject(sp, USER_ELF_BASE, (const void *)0xC0000000u, 4096, PROT_READ, 0), ERR_BADARG,
            "source pointer into the kernel -> BADARG");
    CheckEq(MemInject(sp, 0x40000000u, NULL, 8192, PROT_RW, ASINJECT_FLAG_RESERVE), ZUZU_OK,
            "RESERVE a demand-zero region");
    CheckEq(MemInject(sp, 0x40000000u, NULL, 8192, PROT_RW, ASINJECT_FLAG_RESERVE), ERR_NOMEM,
            "RESERVE over an existing region -> NOMEM");
    CheckEq(MemInject(sp, 0x40000000u, g_code_page, 4096, PROT_RW, 0), ZUZU_OK,
            "inject into a reserved region fills it in place");
    CheckEq(MemInject(sp, 0x50000000u, NULL, 4096, PROT_READ, ASINJECT_FLAG_RESERVE), ZUZU_OK,
            "RESERVE a read-only region");
    CheckEq(MemInject(sp, 0x50000000u, g_code_page, 4096, PROT_RW, 0), ERR_BADARG,
            "inject may not exceed the region's own prot");
    CheckEq(MemInject(-1, USER_ELF_BASE, g_code_page, 4096, PROT_READ, 0), ERR_BADHANDLE,
            "MemInject(invalid space) -> BADHANDLE");
    Handle evt = CreateEvent();
    CheckEq(MemInject(evt, USER_ELF_BASE, g_code_page, 4096, PROT_READ, 0), ERR_BADTYPE,
            "MemInject(event) -> BADTYPE");
    HandleClose(evt);
    SvcResult weak = HandleDuplicate(sp, PERM_WAIT, MARKER_NONE);
    CheckEq(MemInject((Handle)weak.r1, USER_ELF_BASE, g_code_page, 4096, PROT_READ, 0), ERR_NOPERM,
            "MemInject without PERM_CNTL -> NOPERM");
    CheckEq(CreateTask((Handle)weak.r1), ERR_NOPERM,
            "CreateTask without PERM_CNTL on the Space -> NOPERM");
    HandleClose((Handle)weak.r1);
    Handle task = CreateTask(sp);
    Check(task >= 0, "CreateTask in a kitten Space OK");
    CheckEq(MemInject(sp, USER_ELF_BASE, g_code_page, 4096, PROT_READ, 0), ERR_BUSY,
            "injecting once a task exists -> BUSY");
    HandleClose(task);
    CheckEq(HandleDestroy(sp), ZUZU_OK, "Destroy(space) OK");
    CheckEq(HandleQuery(sp, QUERY_TYPE).r0, ERR_BADHANDLE, "the Space handle is gone");

    CheckEq(CreateTask(-2), ERR_BADHANDLE, "CreateTask(invalid space) -> BADHANDLE");
    Handle pt = CreatePort();
    CheckEq(CreateTask(pt), ERR_BADTYPE, "CreateTask(port) -> BADTYPE");
    HandleClose(pt);
    CheckEq(ArchInvokeSvc(SVC_CREATE, OBJECT_CODE_COUNT, 0, 0, 0), ERR_BADARG,
            "Create(unknown object type) -> BADARG");
    CheckEq(ArchInvokeSvc(SVC_CREATE, OBJECT_SPACE, (Register)0xC0000000u, 4, 0), ERR_BADPTR,
            "CreateSpace(kernel name pointer) -> BADPTR");
    Handle longname =
        CreateSpace("a-very-long-space-name-that-is-well-past-the-32-byte-name-field");
    Check(longname >= 0, "an over-long Space name is truncated, not rejected");
    HandleDestroy(longname);
}

/* ---------------- 7b. MemInjectObj ---------------- */

static void TestInjectObj(void)
{
    BeginSection("injectobj");

    Handle obj = CreateMem(1);
    Check(obj >= 0, "CreateMem(1)");
    uint8_t *w = MemMap(obj, 0, PROT_RW);
    Check(!PtrIsErr(w), "map the object to fill it");
    memset(w, 0, 4096);
    memcpy(w, kCodeQuit42, sizeof(kCodeQuit42));

    Handle sp = CreateSpace("zz-injectobj");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE + 1, 0, 4096, PROT_READ), ERR_BADARG,
            "unaligned destination -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 1, 4096, PROT_READ), ERR_BADARG,
            "unaligned offset -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 0, PROT_READ), ERR_BADARG,
            "zero length -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 100, PROT_READ), ERR_BADARG,
            "length not page-aligned -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 4096, PROT_WRITE | PROT_EXEC), ERR_BADARG,
            "W+X -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 4096, (MemProt)8), ERR_BADARG,
            "unknown prot bits -> BADARG");
    CheckEq(MemInjectObj(sp, obj, 0x80000000u, 0, 4096, PROT_READ), ERR_BADARG,
            "destination in kernel space -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 4096, 4096, PROT_READ), ERR_BADARG,
            "offset past the end of the object -> BADARG");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 8192, PROT_READ), ERR_BADARG,
            "length past the end of the object -> BADARG");
    CheckEq(MemInjectObj(-2, obj, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_BADHANDLE,
            "invalid space -> BADHANDLE");
    CheckEq(MemInjectObj(sp, -2, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_BADHANDLE,
            "invalid object -> BADHANDLE");
    Handle evt = CreateEvent();
    CheckEq(MemInjectObj(sp, evt, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_BADTYPE,
            "event as the object -> BADTYPE");
    HandleClose(evt);
    SvcResult weak_obj = HandleDuplicate(obj, PERM_WAIT, MARKER_NONE);
    CheckEq(MemInjectObj(sp, (Handle)weak_obj.r1, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_NOPERM,
            "object without PERM_MAP -> NOPERM");
    HandleClose((Handle)weak_obj.r1);
    SvcResult weak_sp = HandleDuplicate(sp, PERM_WAIT, MARKER_NONE);
    CheckEq(MemInjectObj((Handle)weak_sp.r1, obj, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_NOPERM,
            "space without PERM_CNTL -> NOPERM");
    HandleClose((Handle)weak_sp.r1);

    CheckEq(MemInjectObj(sp, HANDLE_ANON, 0x40000000u, 0, 8192, PROT_RW), ZUZU_OK,
            "HANDLE_ANON reserves a demand-zero region");
    CheckEq(MemInjectObj(sp, HANDLE_ANON, 0x40000000u, 0, 4096, PROT_RW), ERR_NOMEM,
            "overlapping an existing region -> NOMEM");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 4096, PROT_READ | PROT_EXEC), ZUZU_OK,
            "map the object as code");
    CheckEq(MemInjectObj(sp, obj, USER_ELF_BASE, 0, 4096, PROT_READ), ERR_NOMEM,
            "mapping it again over itself -> NOMEM");

    Handle task = CreateTask(sp);
    Check(task >= 0, "CreateTask in the space");
    CheckEq(MemInjectObj(sp, HANDLE_ANON, 0x50000000u, 0, 4096, PROT_RW), ERR_BUSY,
            "mapping once a task exists -> BUSY");

    CheckEq(HandleClose(obj), ZUZU_OK, "close the parent's handle while the child maps it");
    CheckEq(TaskStart(task, (void *)USER_ELF_BASE, (void *)USR_SP, 0, 0), ZUZU_OK,
            "start the task");
    SpaceWaitResult sw = FormatToSpaceWait(WaitOn(sp, 2000));
    CheckEq(sw.status, ZUZU_OK, "the space finishes");
    CheckEq(sw.exit_status, 42, "the code mapped from the object ran");

    HandleClose(task);
    CheckEq(HandleDestroy(sp), ZUZU_OK, "destroy the space");
}

/* ---------------- 8. hostile arguments ---------------- */

static void TestSecurity(void)
{
    BeginSection("hostile arguments");

    Kitten k;
    KittenCreate(&k, kCodeLoop, sizeof(kCodeLoop));
    CheckEq(ArchInvokeSvc(SVC_MANAGETASK, k.task, MNGTASK_START, (Register)0xC0000000u, 0),
            ERR_BADPTR, "Start with a kernel args pointer -> BADPTR");
    CheckEq(ArchInvokeSvc(SVC_MANAGETASK, k.task, MNGTASK_START, 0, 0), ERR_BADPTR,
            "Start with a NULL args pointer -> BADPTR");
    KittenStart(&k, 0, 0);
    CheckEq(TaskGetRegs(k.task, (Register *)0xC0000000u), ERR_BADPTR,
            "GetRegs into kernel memory -> BADPTR");
    CheckEq(TaskGetRegs(k.task, NULL), ERR_BADPTR, "GetRegs into NULL -> BADPTR");
    CheckEq(TaskSetRegs(k.task, (const Register *)0xC0000000u), ERR_BADPTR,
            "SetRegs from kernel memory -> BADPTR");
    KittenFree(&k);

    CheckEq(HandleQuery(0x7FFFFFFF, QUERY_TYPE).r0, ERR_BADHANDLE, "Query(INT_MAX) -> BADHANDLE");
    CheckEq(HandleQuery((Handle)0x80000000u, QUERY_TYPE).r0, ERR_BADHANDLE,
            "Query(INT_MIN) -> BADHANDLE");
    CheckEq(HandleClose((Handle)0xFFFFFC00u), ERR_BADHANDLE,
            "Close(garbage generation) -> BADHANDLE");

    Handle mem = CreateMem(1);
    CheckEq(PtrErr(MemMap(mem, 0xC0000000u, PROT_RW)), ERR_BADARG,
            "MemMap(mem) with a kernel-space hint -> BADARG");
    HandleClose(mem);
    CheckEq(MemUnmap((void *)0xC0000000u), ERR_NOENT, "Unmap(kernel address) -> NOENT");
    CheckEq(MemUnmap(0), ERR_NOENT, "Unmap(NULL) -> NOENT");
    CheckEq(Call(-100, 0, -1).r0, ERR_BADHANDLE, "Call(negative handle) -> BADHANDLE");
    CheckEq(Reply(MSG_BUF_SIZE + 1, -1), ERR_OVERFLOW,
            "Reply(oversize) is rejected before anything else");
}

/* ---------------- 9. sync library (zone / semaphore / condvar) ---------------- */

static Zone g_zone;
static int g_counter;

static int32_t ZoneWorker(void *p)
{
    (void)p;
    for (int i = 0; i < 150; i++) {
        ZoneEnter(&g_zone);
        int v = g_counter;
        if (i % 5 == 0)
            Yield(); /* provoke a lost update if exclusion is broken */
        g_counter = v + 1;
        ZoneExit(&g_zone);
    }
    return 0;
}

static Semaphore g_sem;

static int32_t SemPoster(void *p)
{
    (void)p;
    for (int i = 0; i < 5; i++) {
        Sleep(10);
        SemPost(&g_sem);
    }
    return 0;
}

static CondVariable g_cv;
static Zone g_cv_zone;
static int g_cv_ready;

static int32_t CvWaiter(void *p)
{
    (void)p;
    ZoneEnter(&g_cv_zone);
    while (!g_cv_ready)
        CondVarWait(&g_cv, &g_cv_zone);
    ZoneExit(&g_cv_zone);
    return 1;
}

static void TestSync(void)
{
    BeginSection("sync library");

    CheckEq(ZoneInit(&g_zone), ZUZU_OK, "ZoneInit");
    CheckEq(ZoneTryEnter(&g_zone), ZUZU_OK, "ZoneTryEnter on a free zone");
    CheckEq(ZoneTryEnter(&g_zone), ERR_BUSY, "ZoneTryEnter on a held zone -> BUSY");
    CheckEq(ZoneExit(&g_zone), ZUZU_OK, "ZoneExit");
    g_counter = 0;
    Worker *w[3];
    for (int i = 0; i < 3; i++)
        w[i] = WorkerStart(ZoneWorker, NULL);
    int clean = 0;
    for (int i = 0; i < 3; i++)
        clean += (w[i] && WorkerJoin(w[i]) == 0);
    CheckEq(clean, 3, "three contending threads finish");
    CheckEq(g_counter, 450, "no lost updates under Zone contention");
    CheckEq(ZoneDestroy(&g_zone), ZUZU_OK, "ZoneDestroy");

    CheckEq(SemInit(&g_sem, 2), ZUZU_OK, "SemInit(2)");
    CheckEq(SemWait(&g_sem), ZUZU_OK, "SemWait consumes an initial unit");
    CheckEq(SemWait(&g_sem), ZUZU_OK, "SemWait consumes the second unit");
    Worker *p = WorkerStart(SemPoster, NULL);
    int got = 0;
    for (int i = 0; i < 5; i++)
        got += SemWait(&g_sem) == ZUZU_OK;
    CheckEq(got, 5, "SemWait blocks until each SemPost");
    CheckEq(WorkerJoin(p), 0, "poster exits");
    CheckEq(SemDestroy(&g_sem), ZUZU_OK, "SemDestroy");

    CheckEq(ZoneInit(&g_cv_zone), ZUZU_OK, "ZoneInit (condvar)");
    CheckEq(CondVarInit(&g_cv), ZUZU_OK, "CondVarInit");
    g_cv_ready = 0;
    Worker *cw = WorkerStart(CvWaiter, NULL);
    Sleep(40);
    ZoneEnter(&g_cv_zone);
    g_cv_ready = 1;
    CondVarSignal(&g_cv);
    ZoneExit(&g_cv_zone);
    CheckEq(WorkerJoin(cw), 1, "CondVarWait is woken by CondVarSignal");
    CondVarDestroy(&g_cv);
    ZoneDestroy(&g_cv_zone);
}

/* ---------------- 10. kitten isolation and protection ---------------- */

typedef struct {
    const char *name;
    const uint32_t *code;
    size_t bytes;
    bool with_data;
    TaskWaitOutcome outcome;
    int32_t value; /* exit status, or fault reason */
} KittenCase;

static void TestKittens(void)
{
    BeginSection("kitten isolation");

    const KittenCase cases[] = {
        {"kitten reads the syspage (mapped in every Space)", kCodeReadSyspage,
         sizeof(kCodeReadSyspage), false, TASK_EXITED, 0x50050CA7},
        {"kitten cannot read the rootsvc-only BootInfo page", kCodeReadBootInfo,
         sizeof(kCodeReadBootInfo), false, TASK_FAULTED, KILLED_FAULT_DATA},
        {"kitten cannot read kernel memory", kCodeReadKernel, sizeof(kCodeReadKernel), false,
         TASK_FAULTED, KILLED_FAULT_DATA},
        {"kitten cannot write its own read-exec code page", kCodeWriteCode, sizeof(kCodeWriteCode),
         false, TASK_FAULTED, KILLED_FAULT_DATA},
        {"kitten cannot execute a read-write data page", kCodeExecData, sizeof(kCodeExecData), true,
         TASK_FAULTED, KILLED_FAULT_PREFETCH},
        {"kitten's stack faults in on demand", kCodeStack, sizeof(kCodeStack), false, TASK_EXITED,
         9},
        {"kitten can Yield 100 times", kCodeYieldLoop, sizeof(kCodeYieldLoop), false, TASK_EXITED,
         5},
        {"kitten can Sleep", kCodeSleepQuit, sizeof(kCodeSleepQuit), false, TASK_EXITED, 6},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Kitten k;
        Err rc = KittenCreateEx(&k, cases[i].code, cases[i].bytes, cases[i].with_data);
        if (rc != ZUZU_OK) {
            CheckEq(rc, ZUZU_OK, cases[i].name);
            continue;
        }
        uint64_t t0 = NowMs();
        KittenStart(&k, 0, 0);
        TaskWaitResult tw = FormatToTaskWait(WaitOn(k.task, 3000));
        uint64_t dt = NowMs() - t0;
        bool ok =
            tw.status == ZUZU_OK && tw.outcome == cases[i].outcome && tw.value == cases[i].value;
        if (!ok)
            UserspaceDebugLog(
                "zztest: %s: status=%d outcome=%d value=%d (want outcome=%d value=%d)",
                cases[i].name, tw.status, tw.outcome, tw.value, cases[i].outcome, cases[i].value);
        Check(ok, cases[i].name);
        if (cases[i].code == kCodeSleepQuit)
            Check(dt + TickMs() >= 20, "kitten's Sleep(20) lasted at least 20ms");
        if (tw.outcome == TASK_FAULTED)
            TaskKill(k.task);
        KittenFree(&k);
    }

    /* Many Spaces at once. */
    Kitten many[8];
    int started = 0;
    for (int i = 0; i < 8; i++) {
        if (KittenCreate(&many[i], kCodeQuit42, sizeof(kCodeQuit42)) == ZUZU_OK &&
            KittenStart(&many[i], 0, 0) == ZUZU_OK)
            started++;
    }
    CheckEq(started, 8, "eight kittens run side by side");
    int exited = 0;
    for (int i = 0; i < 8; i++) {
        SpaceWaitResult sw = FormatToSpaceWait(WaitOn(many[i].space, 3000));
        exited += sw.status == ZUZU_OK && sw.exit_status == 42;
        KittenFree(&many[i]);
    }
    CheckEq(exited, 8, "all eight exit with their own status");
}

/* ---------------- 11. task lifetime (use-after-free hunters) ---------------- */

static void TestTaskLifetime(void)
{
    BeginSection("task lifetime");

    /* Thread tasks: kill zombies, close handles, let the reaper run, repeat. */
    int clean = 0;
    for (int i = 0; i < 12; i++) {
        Worker *w = WorkerStart(ArgToExit, (void *)(uintptr_t)i);
        if (!w)
            continue;
        TaskWaitResult tw = FormatToTaskWait(WaitOn(w->task, 2000));
        bool ok = tw.status == ZUZU_OK && tw.value == i;
        ok &= TaskKill(w->task) == ZUZU_OK;
        ok &= TaskKill(w->task) == ZUZU_OK;
        HandleClose(w->task);
        MemUnmap(w->stack);
        w->used = false;
        clean += ok;
        if (i % 3 == 2)
            Sleep(30); /* let the reaper free what we just killed, mid-loop */
    }
    CheckEq(clean, 12, "kill a zombie thread task twice, then close it, 12 times");
    SleepAndSettle();
    Worker *after = WorkerStart(ArgToExit, (void *)(uintptr_t)7);
    CheckEq(after ? WorkerJoin(after) : -1, 7,
            "task slots still work after the reaper freed the killed ones");

    /* Kitten tasks: exit, then kill the zombie, then tear the Space down. */
    for (int i = 0; i < 6; i++) {
        Kitten k;
        KittenCreate(&k, kCodeQuit42, sizeof(kCodeQuit42));
        KittenStart(&k, 0, 0);
        WaitOn(k.space, 2000);
        CheckEq(TaskKill(k.task), ZUZU_OK, "kill the exited kitten's task");
        CheckEq(TaskKill(k.task), ZUZU_OK, "and again");
        KittenFree(&k);
    }

    /* Faulted tasks: kill twice. */
    Kitten f;
    KittenCreate(&f, kCodeFaultData, sizeof(kCodeFaultData));
    KittenStart(&f, 0, 0);
    WaitOn(f.task, 2000);
    CheckEq(TaskKill(f.task), ZUZU_OK, "kill a faulted task");
    CheckEq(TaskKill(f.task), ZUZU_OK, "kill it again");
    CheckEq(TaskResume(f.task), ERR_BADARG, "a killed task cannot be resumed");
    KittenFree(&f);
    SleepAndSettle();

    /* The reaper must still be healthy: churn tasks and check nothing leaked.
     * The warm-up runs the very same round so one-time allocations (slab
     * growth, page-table pages for the new mappings) land before the baseline. */
    uint32_t before = 0;
    for (int i = 0; i < 28; i++) {
        if (i == 3) {
            SleepAndSettle();
            before = PagesFree();
        }
        Worker *w = WorkerStart(ArgToExit, NULL);
        if (w) {
            WaitOn(w->task, 2000);
            TaskKill(w->task);
            HandleClose(w->task);
            MemUnmap(w->stack);
            w->used = false;
        }
        Kitten k;
        if (KittenCreate(&k, kCodeQuit42, sizeof(kCodeQuit42)) == ZUZU_OK) {
            KittenStart(&k, 0, 0);
            WaitOn(k.space, 2000);
            TaskKill(k.task);
            KittenFree(&k);
        }
    }
    SleepAndSettle();
    CheckEq((int32_t)PagesFree(), (int32_t)before,
            "no free-page change after 25 kill-the-zombie rounds (after a 3-round warm-up)");
}

/* ---------------- 11b. many threads in one Space ---------------- */

typedef struct {
    uint32_t id;
    volatile int ready;
    volatile int go;
} ThreadProbe;

/* Writes a per-thread pattern into its own message buffer, sleeps while the
 * other threads do the same, then checks nobody scribbled over it and that
 * its TLS identifies it. */
static int32_t BufferOwner(void *p)
{
    ThreadProbe *t = p;
    uint8_t *buf = GetMessageBox();
    memset(buf, (int)(0x30 + t->id), 64);
    ZuzuTLS()->msg_recv_len = t->id; /* any other per-thread TLS field */
    t->ready = 1;
    while (!t->go)
        Sleep(5);
    for (int i = 0; i < 64; i++) {
        if (buf[i] != (uint8_t)(0x30 + t->id))
            return -1;
    }
    return ZuzuTLS()->msg_recv_len == t->id ? (int32_t)t->id : -2;
}

static void TestManyThreads(void)
{
    BeginSection("many threads");

    enum { N = 12 }; /* more than one TCB page holds */
    static ThreadProbe probes[N];
    Worker *w[N];
    int started = 0;
    for (int i = 0; i < N; i++) {
        probes[i] = (ThreadProbe){.id = (uint32_t)i};
        w[i] = WorkerStart(BufferOwner, &probes[i]);
        started += w[i] != NULL;
    }
    CheckEq(started, N, "12 threads are alive in one Space at once");
    for (int i = 0; i < N; i++) {
        while (w[i] && !probes[i].ready)
            Sleep(5);
        probes[i].go = 1;
    }
    int good = 0;
    for (int i = 0; i < N; i++)
        good += w[i] && WorkerJoin(w[i]) == i;
    CheckEq(good, N, "each thread's message buffer and TLS stayed its own");
}

/* ---------------- 12. ipc stress ---------------- */

static uint32_t Lcg(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

typedef struct {
    Handle port;
    uint32_t seed;
    int rounds;
} StressJob;

/* Every round echoes a random-length, random-content payload and verifies all
 * of it, so a message buffer shared between threads or a stray copy fails. */
static int32_t StressClient(void *p)
{
    StressJob *j = p;
    int32_t bad = 0;
    uint8_t body[MSG_BUF_SIZE];
    for (int r = 0; r < j->rounds; r++) {
        uint32_t len = Lcg(&j->seed) % (MSG_BUF_SIZE - 4 + 1);
        for (uint32_t i = 0; i < len; i++)
            body[i] = (uint8_t)Lcg(&j->seed);
        SvcResult res = Rpc(j->port, OP_ECHO, body, len, -1);
        if (res.r0 != ZUZU_OK || (uint32_t)res.r1 != len) {
            bad++;
            continue;
        }
        const uint8_t *out = GetMessageBox();
        for (uint32_t i = 0; i < len; i++) {
            if (out[i] != (uint8_t)(body[i] ^ 0xFF)) {
                bad++;
                break;
            }
        }
    }
    return bad;
}

typedef struct {
    Handle a, b;
    int rounds;
} PingJob;

static int32_t Ponger(void *p)
{
    PingJob *j = p;
    int n = 0;
    for (int i = 0; i < j->rounds; i++) {
        if (FormatToEventWait(WaitOn(j->a, 2000)).status != ZUZU_OK)
            break;
        Signal(j->b, 1, false);
        n++;
    }
    return n;
}

static int32_t OrderedCaller(void *p)
{
    CallerJob *j = p;
    uint32_t idx = j->op;
    SvcResult r = Rpc(j->port, 0, &idx, sizeof(idx), -1);
    return (Err)r.r0;
}

static void TestStress(void)
{
    BeginSection("ipc stress");

    Server srv = {.port = CreatePort(), .ev = -1};
    Worker *sw = WorkerStart(RunServer, &srv);

    StressJob solo = {.port = srv.port, .seed = 1, .rounds = 300};
    CheckEq(StressClient(&solo), 0, "300 random-size echo round trips, all bytes verified");

    StressJob jobs[3] = {
        {.port = srv.port, .seed = 11, .rounds = 120},
        {.port = srv.port, .seed = 22, .rounds = 120},
        {.port = srv.port, .seed = 33, .rounds = 120},
    };
    Worker *cw[3];
    for (int i = 0; i < 3; i++)
        cw[i] = WorkerStart(StressClient, &jobs[i]);
    int bad = 0;
    for (int i = 0; i < 3; i++)
        bad += cw[i] ? WorkerJoin(cw[i]) : 1;
    CheckEq(bad, 0,
            "three concurrent clients never see each other's data (per-thread message buffers)");

    HandleClose(srv.port);
    WorkerJoin(sw);

    /* Queued callers are served in arrival order. */
    Handle port = CreatePort();
    CallerJob order[4];
    Worker *ow[4];
    for (int i = 0; i < 4; i++) {
        order[i] = (CallerJob){.port = port, .op = (uint32_t)i};
        ow[i] = WorkerStart(OrderedCaller, &order[i]);
        Sleep(30);
    }
    int in_order = 0;
    for (int i = 0; i < 4; i++) {
        PortWaitResult r = FormatToPortWait(WaitOn(port, 2000));
        uint32_t idx = 99;
        if (r.status == ZUZU_OK && r.xlen >= 8)
            memcpy(&idx, (uint8_t *)GetMessageBox() + 4, 4);
        in_order += (r.status == ZUZU_OK && idx == (uint32_t)i);
        Reply(0, -1);
    }
    CheckEq(in_order, 4, "four queued callers are received in FIFO order");
    int served = 0;
    for (int i = 0; i < 4; i++)
        served += ow[i] && WorkerJoin(ow[i]) == ZUZU_OK;
    CheckEq(served, 4, "and every one of them was replied to");
    HandleClose(port);

    /* Event ping-pong: no lost wakeups in either direction. */
    PingJob ping = {.a = CreateEvent(), .b = CreateEvent(), .rounds = 300};
    Worker *pw = WorkerStart(Ponger, &ping);
    int done = 0;
    for (int i = 0; i < 300; i++) {
        Signal(ping.a, 1, false);
        if (FormatToEventWait(WaitOn(ping.b, 2000)).status != ZUZU_OK)
            break;
        done++;
    }
    CheckEq(done, 300, "300 event ping-pong rounds without a lost wakeup");
    CheckEq(WorkerJoin(pw), 300, "the other side saw all 300");
    HandleClose(ping.a);
    HandleClose(ping.b);
}

/* ---------------- 13. leaks ---------------- */

static void LeakRound(void)
{
    Kitten k;
    if (KittenCreate(&k, kCodeLoop, sizeof(kCodeLoop)) == ZUZU_OK) {
        KittenStart(&k, 0, 0);
        Yield();
        KittenFree(&k);
    }
    Handle p = CreatePort();
    Handle e = CreateEvent();
    Handle m = CreateMem(2);
    uint8_t *v = MemMap(m, 0, PROT_RW);
    memset((void *)v, 3, 8192);
    HandleClose(m);
    HandleClose(e);
    HandleClose(p);
    Worker *w = WorkerStart(ArgToExit, NULL);
    if (w)
        WorkerJoin(w);
}

static void TestLeaks(void)
{
    BeginSection("leaks");

    LeakRound(); /* warm-up: lazy one-time kernel allocations land here */
    LeakRound();
    SleepAndSettle();
    uint32_t before = PagesFree();
    for (int i = 0; i < 15; i++)
        LeakRound();
    SleepAndSettle();
    uint32_t after = PagesFree();
    CheckEq((int32_t)after, (int32_t)before,
            "free pages unchanged after 15 Space/Task/Port/Event/Mem rounds");
    if (before != after)
        UserspaceDebugLog("zztest: leak delta = %d pages", (int)before - (int)after);
}

/* ---------------- main ---------------- */

#define PASSES 3

static void RunSuite(void)
{
    TestBasics();
    TestHandles();
    TestMemory();
    TestEvents();
    TestIpc();
    TestObservers();
    TestTasks();
    TestSpaces();
    TestInjectObj();
    TestKittens();
    TestTaskLifetime();
    TestManyThreads();
    TestStress();
    TestSecurity();
    TestSync();
    TestLeaks();
}

int main(void)
{
    UserspaceDebugLog("zztest: Prowl syscall suite starting (%d passes)", PASSES);

    int pass_total = 0, fail_total = 0;
    uint32_t pages_at_pass_end[PASSES];
    for (int pass = 0; pass < PASSES; pass++) {
        g_cur = -1;
        g_verbose = (pass == 0); /* later passes only report failures */
        UserspaceDebugLog("zztest: ===== pass %d =====", pass + 1);
        RunSuite();
        EndSection();

        int pass_ok = 0, pass_fail = 0;
        for (int i = 0; i <= g_cur; i++) {
            if (pass == 0)
                UserspaceDebugLog("zztest: %-22s %3d pass %3d fail", g_sections[i].name,
                                  g_sections[i].pass, g_sections[i].fail);
            pass_ok += g_sections[i].pass;
            pass_fail += g_sections[i].fail;
        }
        UserspaceDebugLog("zztest: pass %d: %d pass %d fail", pass + 1, pass_ok, pass_fail);
        pass_total += pass_ok;
        fail_total += pass_fail;
        SleepAndSettle();
        pages_at_pass_end[pass] = PagesFree();
    }

    /* Pass 1 absorbs one-time kernel allocations; after that the suite as a
     * whole must not lose a single page. */
    int32_t drift = (int32_t)pages_at_pass_end[PASSES - 2] - (int32_t)pages_at_pass_end[PASSES - 1];
    UserspaceDebugLog("zztest: whole-suite page drift between the last two passes: %d", drift);
    if (drift != 0)
        fail_total++;

    UserspaceDebugLog("zztest: TOTAL %d pass %d fail -> %s", pass_total, fail_total,
                      fail_total ? "FAILED" : "ALL PASSED");
    return fail_total;
}
