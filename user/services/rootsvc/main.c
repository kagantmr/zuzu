#include "rootsvc.h"
#include <cpio.h>
#include <fs/fsd_client.h>
#include <string.h>
#include <ztask/ztask.h>
#include <util/spawn.h>
#include <util/tls.h>
#include <zuzu/bootinfo.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/syspage.h>
#include <zuzu/udbg.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define MAX_KITTENS 30
#define STACK_SIZE (16 * 1024)
#define INITRD_VA (USER_MMAP_BASE + (MAX_TCB_PAGES * PAGE_SIZE))
#define SD_SERVICES_MANIFEST "/etc/services.manifest"
#define MANIFEST_PATH_MAX 64

typedef enum {
    SOURCE_INITRD,
    SOURCE_SD,
} ImageSource;

typedef struct {
    bool active;
    Handle task;
    Handle space;
    char path[48];
} Kitten;

static Kitten g_kittens[MAX_KITTENS];
static uint32_t g_kitten_count;
static Handle g_monitor_ev;
static FsdConn g_fsd;
const BootInfo *g_bootinfo;

static void MonitorKitten(Handle task, Handle space, const char *path)
{
    if (g_kitten_count >= MAX_KITTENS) {
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

static void SpawnKitten(const void *zxf_data, size_t zxf_size, const char *path,
                        const SpawnAttr *attr)
{
    Spid pid;
    Handle task;
    Err rc = SpawnProcessAttr(zxf_data, zxf_size, path, NULL, 0, 0, attr, &pid, &task);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("rootsvc: failed to spawn %s: %d", path, rc);
        return;
    }
    UserspaceDebugLog("rootsvc: spawned %s", path);
    MonitorKitten(task, (Handle)pid, path);
}

static void SpawnKittenFromSd(const char *path, const SpawnAttr *attr)
{
    char sd_path[MANIFEST_PATH_MAX + 1];
    size_t n = strlen(path);
    if (n >= MANIFEST_PATH_MAX)
        return;
    sd_path[0] = '/';
    memcpy(sd_path + 1, path, n + 1);

    Handle obj;
    size_t len;
    Err rc = FsdReadImage(&g_fsd, sd_path, &obj, &len);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("rootsvc: missing SD program %s: %d", sd_path, rc);
        return;
    }

    Spid pid;
    Handle task;
    rc = SpawnProcessObjAttr(obj, len, path, NULL, 0, 0, attr, &pid, &task);
    HandleClose(obj);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("rootsvc: failed to spawn %s: %d", sd_path, rc);
        return;
    }
    UserspaceDebugLog("rootsvc: spawned %s from SD", sd_path);
    MonitorKitten(task, (Handle)pid, path);
}

static const char *BaseName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool KittenNamed(const char *path)
{
    const char *name = BaseName(path);
    for (uint32_t i = 0; i < g_kitten_count; i++) {
        if (g_kittens[i].active && strcmp(BaseName(g_kittens[i].path), name) == 0)
            return true;
    }
    return false;
}

static void HandleKittenFault(Kitten *k)
{
    TaskRegs regs;
    Err rc = TaskGetRegs(k->task, &regs);
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
    return sp->kernel_ver >= 0x00020000; /* major minor patch: gotta be at least 0x00 00 02 00 */
}

static bool ParseUint(const char *s, const char *end, int32_t *out)
{
    if (s >= end)
        return false;
    int32_t v = 0;
    for (; s < end; s++) {
        if (*s < '0' || *s > '9')
            return false;
        v = (v * 10) + (*s - '0');
        if (v > 255)
            return false;
    }
    *out = v;
    return true;
}

static bool FlagSet(const char *p, const char *end, const char *name)
{
    size_t nlen = strlen(name);
    while (p < end) {
        const char *tok_end = p;
        while (tok_end < end && *tok_end != ',')
            tok_end++;
        if ((size_t)(tok_end - p) == nlen && strncmp(p, name, nlen) == 0)
            return true;
        p = tok_end + 1;
    }
    return false;
}

static bool ParseFlags(const char *p, const char *end, SpawnAttr *attr)
{
    while (p < end) {
        const char *tok_end = p;
        while (tok_end < end && *tok_end != ',')
            tok_end++;
        size_t len = (size_t)(tok_end - p);
        if (len > 5 && strncmp(p, "prio=", 5) == 0) {
            if (!ParseUint(p + 5, tok_end, &attr->prio))
                return false;
        } else if (len > 8 && strncmp(p, "maxprio=", 8) == 0) {
            if (!ParseUint(p + 8, tok_end, &attr->max_prio))
                return false;
        }
        p = tok_end + 1;
    }
    return true;
}

#define MAX_AWAITED 16
#define AWAIT_STEP_MS 5
#define AWAIT_TIMEOUT_MS 3000

static char g_awaited[MAX_AWAITED][NS_MAX_PATH];
static uint32_t g_awaited_count;

static void AwaitServices(void)
{
    for (uint32_t i = 0; i < g_awaited_count; i++) {
        Duration waited = 0;
        for (;;) {
            Handle h = LookupService(g_awaited[i]);
            if (h >= 0) {
                HandleClose(h);
                break;
            }
            if (waited >= AWAIT_TIMEOUT_MS) {
                UserspaceDebugLog("rootsvc: %s never registered, continuing", g_awaited[i]);
                break;
            }
            Sleep(AWAIT_STEP_MS);
            waited += AWAIT_STEP_MS;
        }
    }
}

static void SpawnPass(const char *manifest, size_t manifest_size, ImageSource source,
                      bool late_pass)
{
    const void *initrd_base = (const void *)INITRD_VA;
    size_t initrd_size = g_bootinfo->initrd_size;

    const char *line = manifest;
    const char *end = line + manifest_size;

    while (line < end) {
        const char *line_end = line;
        while (line_end < end && *line_end != '\n')
            line_end++;
        size_t line_len = (size_t)(line_end - line);
        const char *next = line_end + 1;

        const char *pipe = line_len ? memchr(line, '|', line_len) : NULL;
        if (line_len == 0 || line[0] == '#' || !pipe) {
            line = next;
            continue;
        }

        size_t path_len = (size_t)(pipe - line);
        char path[MANIFEST_PATH_MAX];
        if (path_len >= sizeof(path)) {
            line = next;
            continue;
        }
        memcpy(path, line, path_len);
        path[path_len] = '\0';

        const char *role = pipe + 1;
        const char *field_end = memchr(role, '|', (size_t)(line_end - role));
        const char *svc_path = field_end ? field_end + 1 : NULL;
        if (!field_end)
            field_end = line_end;

        SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT, .max_prio = SPAWN_PRIO_INHERIT};
        const char *colon = memchr(role, ':', (size_t)(field_end - role));
        bool late = false;
        if (colon) {
            if (!ParseFlags(colon + 1, field_end, &attr)) {
                UserspaceDebugLog("rootsvc: bad flags for %s, skipping", path);
                line = next;
                continue;
            }
            late = FlagSet(colon + 1, field_end, "late");
        }

        if (strncmp(role, "file", 4) == 0 || late != late_pass) {
            line = next;
            continue;
        }

        if (source == SOURCE_SD) {
            if (KittenNamed(path)) {
                UserspaceDebugLog("rootsvc: %s already runs from initrd, skipping SD copy", path);
                line = next;
                continue;
            }
            SpawnKittenFromSd(path, &attr);
        } else {
            const void *zxf_data;
            size_t zxf_size;
            if (!CpioFind(initrd_base, initrd_size, path, &zxf_data, &zxf_size)) {
                UserspaceDebugLog("rootsvc: missing boot program %s", path);
                line = next;
                continue;
            }
            SpawnKitten(zxf_data, zxf_size, path, &attr);
        }

        if (!late_pass && svc_path && svc_path < line_end && g_awaited_count < MAX_AWAITED) {
            size_t n = (size_t)(line_end - svc_path);
            while (n > 0 && (svc_path[n - 1] == '\r' || svc_path[n - 1] == ' '))
                n--;
            if (n > 0 && n < NS_MAX_PATH) {
                memcpy(g_awaited[g_awaited_count], svc_path, n);
                g_awaited[g_awaited_count][n] = '\0';
                g_awaited_count++;
            }
        }

        line = next;
    }
}

static bool OpenSdManifest(Handle *obj, const char **data, size_t *size)
{
    Err rc = FsdConnect(&g_fsd, FSD_SHM_DEFAULT);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("rootsvc: fsd unavailable (%d), no SD services", rc);
        return false;
    }

    rc = FsdReadImage(&g_fsd, SD_SERVICES_MANIFEST, obj, size);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("rootsvc: no %s (%d), no SD services", SD_SERVICES_MANIFEST, rc);
        return false;
    }

    void *view = MemMap(*obj, 0, PROT_READ);
    if (PtrIsErr(view)) {
        HandleClose(*obj);
        return false;
    }
    *data = (const char *)view;
    return true;
}

static void SpawnStages(void)
{
    const void *boot_manifest;
    size_t boot_size;
    if (!CpioFind((const void *)INITRD_VA, g_bootinfo->initrd_size, "boot.manifest",
                  &boot_manifest, &boot_size)) {
        UserspaceDebugLog("rootsvc: no boot.manifest in initrd");
        return;
    }

    SpawnPass(boot_manifest, boot_size, SOURCE_INITRD, false);
    AwaitServices();

    Handle sd_obj = -1;
    const char *sd_manifest = NULL;
    size_t sd_size = 0;
    if (OpenSdManifest(&sd_obj, &sd_manifest, &sd_size)) {
        g_awaited_count = 0;
        SpawnPass(sd_manifest, sd_size, SOURCE_SD, false);
        AwaitServices();
    }

    SpawnPass(boot_manifest, boot_size, SOURCE_INITRD, true);

    if (sd_manifest) {
        SpawnPass(sd_manifest, sd_size, SOURCE_SD, true);
        MemUnmap((void *)(uintptr_t)sd_manifest);
        HandleClose(sd_obj);
    }
    FsdDetach(&g_fsd);
}

int main(void)
{
    UserspaceDebugLog("rootsvc: started");
    if (!VersionOk()) {
        UserspaceDebugLog("rootsvc: kernel too old");
        Quit(ERR_BADARG);
    }

    Handle nsvc = NsvcInit();
    UserspaceDebugLog("rootsvc: nsvc port=%d", nsvc);
    if (nsvc != NSVC_PORT)
        Quit(ERR_BADARG);

    g_bootinfo = (const BootInfo *)USER_BOOTINFO_VA;
    g_monitor_ev = CreateEvent();

    ZTaskSpawnDetached(DevsvcMain, NULL, STACK_SIZE);
    ZTaskSpawnDetached(NsvcMain, NULL, STACK_SIZE);

    SpawnStages();
    UserspaceDebugLog("rootsvc: spawned %u kitten(s), monitoring", g_kitten_count);

    for (;;) {
        EventWaitResult r = FormatToEventWait(WaitOn(g_monitor_ev, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK)
            continue;
        for (uint32_t bit = 0; bit < g_kitten_count; bit++) {
            if (!(r.bits & (1U << bit)) || !g_kittens[bit].active)
                continue;
            CheckKittenState(&g_kittens[bit]);
        }
    }
}
