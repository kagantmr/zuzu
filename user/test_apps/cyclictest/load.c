#include <stdio.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/user_layout.h>

#include "cyclictest.h"

#define MEM_BYTES (4u * 1024u * 1024u)
#define VM_PAGES 256u
#define SPACE_PAGES 256u
#define IPC_LEN 512u
#define FS_BYTES (64u * 1024u)
#define FS_PATH_FMT "/cyclictest%u.tmp"

static const Load kLoads[] = {
    {"idle", "no load, baseline", ""},
    {"cpu", "user-mode spin loops", "cpu"},
    {"syscall", "short syscalls in a tight loop", "syscall"},
    {"mem", "4 MB memcpy per worker (cache/DRAM pressure)", "mem"},
    {"vm", "map 1 MB anon, fault every page, unmap", "vm"},
    {"space", "touch 256 fresh pages, inject, destroy (frees them)", "space"},
    {"ipc", "512 B same-space IPC ping-pong", "ipc"},
    {"fs", "64 KB SD write + read through fsd", "fs"},
    {"all", "every load above at once", "cpu,syscall,mem,vm,space,ipc,fs"},
};

const Load *LoadAt(uint32_t i) { return i < ARRAY_LEN(kLoads) ? &kLoads[i] : NULL; }

uint32_t LoadCount(void) { return ARRAY_LEN(kLoads); }

const Load *LoadFind(const char *name)
{
    for (uint32_t i = 0; i < ARRAY_LEN(kLoads); i++)
        if (!strcmp(kLoads[i].name, name))
            return &kLoads[i];
    return NULL;
}

static void Finish(Worker *w, Err e)
{
    w->err = e;
    w->done = 1;
    Quit(0);
}

static void CpuWorker(Worker *w)
{
    while (!g_stop)
        w->ops++;
    Finish(w, ZUZU_OK);
}

static void SyscallWorker(Worker *w)
{
    Handle ev = CreateEvent();
    if (ev < 0)
        Finish(w, ev);
    while (!g_stop) {
        SvcResult d = HandleDuplicate(ev, PERM_SEND, MARKER_NONE);
        if (d.r0 == ZUZU_OK)
            HandleClose((Handle)d.r1);
        Signal(ev, 1, false);
        WaitOnEvent(ev, TIMEOUT_POLL, 1, false);
        w->ops++;
    }
    HandleClose(ev);
    Finish(w, ZUZU_OK);
}

static void MemWorker(Worker *w)
{
    uint8_t *buf = MemMapAnon(MEM_BYTES, 0, PROT_RW);
    if (PtrIsErr(buf))
        Finish(w, ERR_NOMEM);
    uint32_t half = MEM_BYTES / 2;
    memset(buf, 0x5A, MEM_BYTES);
    while (!g_stop) {
        memcpy(buf + (w->ops & 1 ? 0 : half), buf + (w->ops & 1 ? half : 0), half);
        w->ops++;
    }
    MemUnmap(buf);
    Finish(w, ZUZU_OK);
}

static void VmWorker(Worker *w)
{
    while (!g_stop) {
        volatile uint8_t *p = MemMapAnon(VM_PAGES * PAGE, 0, PROT_RW);
        if (PtrIsErr((void *)p))
            Finish(w, ERR_NOMEM);
        for (uint32_t i = 0; i < VM_PAGES; i++)
            p[i * PAGE] = (uint8_t)i;
        MemUnmap((void *)p);
        w->ops++;
    }
    Finish(w, ZUZU_OK);
}

static void SpaceWorker(Worker *w)
{
    while (!g_stop) {
        Handle obj = CreateMem(SPACE_PAGES, 0);
        if (obj < 0)
            Finish(w, obj);
        uint8_t *m = MemMap(obj, 0, PROT_RW);
        if (PtrIsErr(m)) {
            HandleClose(obj);
            Finish(w, ERR_NOMEM);
        }
        for (uint32_t i = 0; i < SPACE_PAGES; i++)
            m[i * PAGE] = (uint8_t)i;
        MemUnmap(m);
        Handle sp = CreateSpace("cyclictest-load");
        if (sp < 0) {
            HandleClose(obj);
            Finish(w, sp);
        }
        Err rc = MemInjectObj(sp, obj, USER_ELF_BASE, 0, SPACE_PAGES * PAGE, PROT_RW);
        HandleClose(obj);
        HandleDestroy(sp);
        if (rc != ZUZU_OK)
            Finish(w, rc);
        w->ops++;
    }
    Finish(w, ZUZU_OK);
}

static void IpcServer(Worker *w)
{
    PortWaitResult r = FormatToPortWait(WaitOn(w->port, TIMEOUT_INFINITE));
    for (;;) {
        if (r.status != ZUZU_OK)
            Finish(w, r.status);
        w->ops++;
        r = FormatToPortWait(ReplyRecv(r.xlen, -1, w->port, TIMEOUT_INFINITE));
    }
}

static void IpcClient(Worker *w)
{
    memset(GetMessageBox(), 0xA5, IPC_LEN);
    while (!g_stop) {
        SvcResult r = Call(w->port, IPC_LEN, -1);
        if (r.r0 != ZUZU_OK)
            Finish(w, (Err)r.r0);
        w->ops++;
    }
    Finish(w, ZUZU_OK);
}

static void FsWorker(Worker *w)
{
    static uint32_t s_id;
    FsdConn c;
    Err rc = FsdConnect(&c, FSD_SHM_DEFAULT);
    if (rc != ZUZU_OK)
        Finish(w, rc);
    char path[32];
    snprintf(path, sizeof(path), FS_PATH_FMT, (unsigned)s_id++);
    uint8_t *buf = MemMapAnon(FS_BYTES, 0, PROT_RW);
    if (PtrIsErr(buf))
        Finish(w, ERR_NOMEM);
    for (uint32_t i = 0; i < FS_BYTES; i++)
        buf[i] = (uint8_t)(i * 31u + 7u);
    uint32_t chunk = c.size - FSD_DATA_OFF;
    while (!g_stop && rc == ZUZU_OK) {
        uint32_t fd;
        rc = FsdOpen(&c, path, FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS, &fd);
        for (uint32_t done = 0; rc == ZUZU_OK && done < FS_BYTES;) {
            uint32_t want = FS_BYTES - done < chunk ? FS_BYTES - done : chunk;
            uint32_t put = 0;
            rc = FsdWrite(&c, fd, buf + done, want, &put);
            if (rc == ZUZU_OK && put == 0)
                rc = ERR_IO;
            done += put;
        }
        if (rc == ZUZU_OK)
            FsdClose(&c, fd);
        if (rc == ZUZU_OK)
            rc = FsdOpen(&c, path, FSD_MODE_READ, &fd);
        for (uint32_t done = 0; rc == ZUZU_OK && done < FS_BYTES;) {
            uint32_t want = FS_BYTES - done < chunk ? FS_BYTES - done : chunk;
            uint32_t got = 0;
            rc = FsdRead(&c, fd, buf + done, want, &got);
            if (rc == ZUZU_OK && got == 0)
                rc = ERR_IO;
            done += got;
        }
        if (rc == ZUZU_OK)
            FsdClose(&c, fd);
        w->ops++;
    }
    FsdUnlink(&c, path);
    MemUnmap(buf);
    Finish(w, rc);
}

typedef struct {
    const char *kind;
    WorkerFn fn;
    bool single;
} Kind;

static const Kind kKinds[] = {
    {"cpu", CpuWorker, false},     {"syscall", SyscallWorker, false},
    {"mem", MemWorker, false},     {"vm", VmWorker, false},
    {"space", SpaceWorker, false}, {"ipc", IpcClient, false},
    {"fs", FsWorker, true},
};

static bool Spawn(Worker *w, const char *kind, WorkerFn fn, uint32_t prio, Handle port)
{
    memset(w, 0, sizeof(*w));
    w->fn = fn;
    w->kind = kind;
    w->port = port;
    w->stack = MemMapAnon(WORKER_STACK, 0, PROT_RW);
    if (PtrIsErr(w->stack)) {
        w->task = -1;
        return false;
    }
    w->stack[WORKER_STACK - 1] = 0;
    w->task = CreateTask(-1);
    if (w->task < 0)
        return false;
    if (TaskSetPriority(w->task, prio) != ZUZU_OK ||
        TaskStart(w->task, (void *)fn, w->stack + WORKER_STACK, (Register)(VirtAddr)w, 0) !=
            ZUZU_OK) {
        HandleClose(w->task);
        w->task = -1;
        return false;
    }
    return true;
}

static bool HasKind(const char *list, const char *kind)
{
    size_t n = strlen(kind);
    for (const char *p = list; *p;) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, kind, n))
            return true;
        if (!e)
            break;
        p = e + 1;
    }
    return false;
}

uint32_t LoadStart(const Load *l, uint32_t per_kind, uint32_t prio, Worker *ws, uint32_t max)
{
    g_stop = 0;
    uint32_t n = 0;
    for (uint32_t k = 0; k < ARRAY_LEN(kKinds); k++) {
        const Kind *kd = &kKinds[k];
        if (!HasKind(l->kinds, kd->kind))
            continue;
        uint32_t count = kd->single ? 1 : per_kind;
        Handle port = -1;
        if (kd->fn == IpcClient) {
            port = CreatePort();
            if (port < 0 || n >= max) {
                printf("cyclictest: ipc port failed: %d\n", (int)port);
                continue;
            }
            if (!Spawn(&ws[n], "ipc-server", IpcServer, prio, port)) {
                printf("cyclictest: ipc server start failed\n");
                HandleClose(port);
                continue;
            }
            n++;
        }
        for (uint32_t i = 0; i < count && n < max; i++) {
            if (Spawn(&ws[n], kd->kind, kd->fn, prio, port))
                n++;
            else
                printf("cyclictest: %s worker start failed\n", kd->kind);
        }
    }
    return n;
}

void LoadStop(Worker *ws, uint32_t n)
{
    g_stop = 1;
    for (uint32_t i = 0; i < n; i++) {
        Worker *w = &ws[i];
        if (w->task < 0 || w->fn == IpcServer)
            continue;
        if (WaitOn(w->task, 5000).r0 != ZUZU_OK) {
            printf("cyclictest: %s worker did not stop, killing\n", w->kind);
            TaskKill(w->task);
        }
    }
    for (uint32_t i = 0; i < n; i++)
        if (ws[i].task >= 0 && ws[i].fn == IpcServer)
            TaskKill(ws[i].task);
    for (uint32_t i = 0; i < n; i++) {
        Worker *w = &ws[i];
        if (w->task >= 0)
            HandleClose(w->task);
        if (w->fn == IpcServer)
            HandleClose(w->port);
        if (!PtrIsErr(w->stack))
            MemUnmap(w->stack);
    }
}
