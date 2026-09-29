#include <cpio.h>
#include <string.h>
#include <util/tls.h>
#include <util/zxf_spawn.h>
#include <zuzu/bootinfo.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <zuzu/udbg.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define MAX_KITTENS 30
#define STACK_SIZE (16 * 1024)
#define INITRD_VA (USER_MMAP_BASE + (MAX_TCB_PAGES * PAGE_SIZE))

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

/* Filled in once devsvc/nsvc's real logic gets ported. */
static void DevsvcMain(void)
{
    for (;;)
        Yield();
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
    TaskWaitResult tw = AsTaskWait(WaitOn(k->task, 0));
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
            /* rootsvc's own no-pipe entry -- the kernel loader already
             * consumed this one, skip it exactly once. */
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
        SvcResult r = WaitOn(g_monitor_ev, TIMEOUT_INFINITE);
        if (r.r0 != ZUZU_OK)
            continue;
        EventWord bits = (EventWord)r.r1;
        for (uint32_t bit = 0; bit < g_kitten_count; bit++)
        {
            if (!(bits & (1U << bit)) || !g_kittens[bit].active)
                continue;
            CheckKittenState(&g_kittens[bit]);
        }
    }
}
