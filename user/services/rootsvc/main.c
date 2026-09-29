#include <cpio.h>
#include <string.h>
#include <util/tls.h>
#include <util/zxf_spawn.h>
#include <zuzu/bootinfo.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <util/msg.h>
#include <zuzu/udbg.h>
#include <dev/protocols/devm.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define MAX_KITTENS 30
#define STACK_SIZE (16 * 1024)
#define INITRD_VA (USER_MMAP_BASE + (MAX_TCB_PAGES * PAGE_SIZE))
#define DEVICE_HANDLE_BASE 16

typedef struct
{
    bool active;
    Handle task;
    Handle space;
    char path[48];
} Kitten;

static Kitten g_kittens[MAX_KITTENS];
static uint32_t g_kitten_count;
static Handle g_monitor_ev;
static const BootInfo *g_bootinfo;

static Handle g_devsvc_port;

static int DevmUnpack(const char *buf, uint32_t xlen, DevmRequest *out)
{
    /* 1. Header must fit: cmd(4) + count(4). */
    if (xlen < 8) {
        return ERR_BADARG;
    }

    uint32_t cmd;
    uint32_t count;
    memcpy(&cmd,   buf,     4);   /* alignment- and aliasing-safe */
    memcpy(&count, buf + 4, 4);

    /* 2. Bound count before it indexes strings[]. */
    if (count == 0 || count > DEVM_MAX_COMPAT) {
        return ERR_BADARG;
    }

    /* 3. Bounded walk of `count` NUL-terminated strings. */
    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (off >= xlen) {
            return ERR_BADARG;              /* ran out before string i */
        }

        uint32_t remaining = xlen - off;
        size_t   len       = strnlen(buf + off, remaining);
        if (len == remaining) {
            return ERR_BADARG;              /* no NUL within bounds */
        }

        out->strings[i] = buf + off;
        off += (uint32_t)len + 1;           /* +1 steps over the NUL */
    }


    if (off != xlen) {
        return ERR_BADARG;
    }

    out->cmd   = (DevmRequestType)cmd;
    out->count = count;
    return ZUZU_OK;   
}
static void DevsvcMain(void)
{
    g_devsvc_port = CreatePort();
    if (g_devsvc_port < 0)
        return;
    HandleDuplicate(g_devsvc_port, PERM_MAP, 0xDE71CE00);

    for (;;)
    {
        PortWaitResult result = FormatToPortWait(WaitOn(g_devsvc_port, TIMEOUT_INFINITE));
        if (result.status != ZUZU_OK)
            continue;

        DevmRequest req;
        if (DevmUnpack(MessageBuf(), result.xlen, &req) != ZUZU_OK)
            continue;

        switch (req.cmd)
        {
            case DEVM_REQUEST: {
                uint32_t matched_index = 0;
                bool found = false;
                for (uint32_t i = 0; i < req.count && !found; i++) {
                    for (uint32_t d = 0; d < g_bootinfo->dev_count; d++) {
                        if (strcmp(req.strings[i], g_bootinfo->devs[d].compatible) == 0) {
                            matched_index = d;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                    break;

                SvcResult dup = HandleDuplicate(DEVICE_HANDLE_BASE + matched_index, PERM_MAP, MARKER_NONE);
                if (dup.r0 != ZUZU_OK)
                    break;

                memcpy(MessageBuf(), &matched_index, sizeof(matched_index));
                Reply(sizeof(matched_index), (Handle)dup.r1);
            } break;
            default:
                break;
        }
    }
}

static void NsvcMain(void)
{
    for (;;)
        Yield();
}

static Handle SpawnThread(void (*entry)(void))
{
    VirtAddr stack = MemMapAnon(STACK_SIZE, 0, PROT_READ | PROT_WRITE);
    if (PtrIsErr((void *)stack))
        return (Handle)stack;

    Handle h = CreateTask(-1);
    if (h < 0)
        return h;

    Err rc = TaskStart(h, (VirtAddr)entry, stack + STACK_SIZE, 0, 0);
    if (rc != ZUZU_OK)
    {
        HandleClose(h);
        return rc;
    }
    return h;
}

static void MonitorKitten(Handle task, Handle space, const char *path)
{
    if (g_kitten_count >= MAX_KITTENS)
    {
        UserspaceDebugLog("rootsvc: too many kittens, dropping %s", path);
        HandleDestroy(space);
        return;
    }
    uint32_t bit = g_kitten_count++;
    Kitten *k = &g_kittens[bit];
    k->active = true;
    k->task = task;
    k->space = space;
    strncpy(k->path, path, sizeof(k->path) - 1);
    k->path[sizeof(k->path) - 1] = '\0';

    Bind(EVENT_TASK, g_monitor_ev, task, bit);
    Bind(EVENT_SPACE, g_monitor_ev, space, bit);
}

static void SpawnKitten(const void *zxf_data, size_t zxf_size, const char *path)
{
    Spid pid;
    Handle task;
    Err rc = ZxfSpawn(zxf_data, zxf_size, path, NULL, 0, 0, &pid, &task);
    if (rc != ZUZU_OK)
    {
        UserspaceDebugLog("rootsvc: failed to spawn %s: %d", path, rc);
        return;
    }
    MonitorKitten(task, (Handle)pid, path);
}

static void HandleKittenFault(Kitten *k)
{
    Register regs[ARCH_NUM_GP_REGS];
    Err rc = TaskGetRegs(k->task, regs);
    UserspaceDebugLog("rootsvc: %s faulted (get_regs rc=%d)", k->path, rc);

    /* No recovery policy yet so just take the kitten down. Once there's
     * something to actually inspect/patch the fault with, this is where
     * TaskSetRegs + TaskResume would go instead. */
    HandleDestroy(k->space);
    k->active = false;
}

static void ReapKitten(Kitten *k)
{
    HandleClose(k->task);
    HandleClose(k->space);
    k->active = false;
}

static void CheckKittenState(Kitten *k)
{
    TaskWaitResult tw = FormatToTaskWait(WaitOn(k->task, 0));
    if (tw.status != ZUZU_OK)
        return;

    if (tw.outcome == TASK_FAULTED) {
        HandleKittenFault(k); /* tw.value = fault reason */
    } else {
        ReapKitten(k); /* tw.value = exit_status */
    }
}

static bool VersionOk(void)
{
    const Syspage *sp = (const Syspage *)SYSPAGE_VA;
    return sp->kernel_ver >= 0x00000200; /* major minor patch: gotta be at least 0x00 00 02 00 */
}

static void SpawnStage1(void)
{
    const void *initrd_base = (const void *)INITRD_VA;
    size_t initrd_size = g_bootinfo->initrd_size;

    const void *manifest_data;
    size_t manifest_size;
    if (!cpio_find(initrd_base, initrd_size, "boot.manifest", &manifest_data, &manifest_size))
    {
        UserspaceDebugLog("rootsvc: no boot.manifest in initrd");
        return;
    }

    const char *line = (const char *)manifest_data;
    const char *end = line + manifest_size;
    bool skipped_own_entry = false;

    while (line < end)
    {
        const char *line_end = line;
        while (line_end < end && *line_end != '\n')
            line_end++;
        size_t line_len = (size_t)(line_end - line);

        if (line_len == 0 || line[0] == '#')
        {
            line = line_end + 1;
            continue;
        }

        const char *pipe = memchr(line, '|', line_len);
        if (!pipe)
        {
            if (!skipped_own_entry)
                skipped_own_entry = true;
            line = line_end + 1;
            continue;
        }

        size_t path_len = (size_t)(pipe - line);
        char path[64];
        if (path_len >= sizeof(path))
        {
            line = line_end + 1;
            continue;
        }
        memcpy(path, line, path_len);
        path[path_len] = '\0';

        const char *role = pipe + 1;
        if (strncmp(role, "file", 4) == 0)
        {
            line = line_end + 1;
            continue; /* packed but never spawned */
        }

        const void *zxf_data;
        size_t zxf_size;
        if (!cpio_find(initrd_base, initrd_size, path, &zxf_data, &zxf_size))
        {
            UserspaceDebugLog("rootsvc: missing boot program %s", path);
            line = line_end + 1;
            continue;
        }

        SpawnKitten(zxf_data, zxf_size, path);

        line = line_end + 1;
    }
}

int main(void)
{
    if (!VersionOk())
        Quit(ERR_BADARG);

    g_bootinfo = (const BootInfo *)USER_BOOTINFO_VA;
    g_monitor_ev = CreateEvent();

    SpawnThread(DevsvcMain);
    SpawnThread(NsvcMain);

    SpawnStage1();

    for (;;)
    {
        EventWaitResult r = FormatToEventWait(WaitOn(g_monitor_ev, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK)
            continue;
        for (uint32_t bit = 0; bit < g_kitten_count; bit++)
        {
            if (!(r.bits & (1U << bit)) || !g_kittens[bit].active)
                continue;
            CheckKittenState(&g_kittens[bit]);
        }
    }
}
