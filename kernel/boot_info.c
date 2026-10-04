#include "boot_info.h"
#include "kernel/dev/fdt_wrappers.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <libfdt.h>
#include <stddef.h>
#include <string.h>
#include <zuzu/bootinfo.h>

#define LOG_FMT(fmt) "(boot_info) " fmt
#include "core/log.h"

static KernelBootInfo g_boot_info = {0};
static PhysAddr g_bootinfo_pa;

static void collect_dev_cb(const char *compatible, const char *path, uint64_t phys, uint64_t size,
                           uint32_t irq)
{
    if (!g_boot_info.devs)
        return;
    uint32_t idx = g_boot_info.count;
    FdtDevice *d = &g_boot_info.devs[idx];
    strncpy(d->compatible, compatible ? compatible : "", sizeof(d->compatible) - 1);
    d->compatible[sizeof(d->compatible) - 1] = '\0';
    d->phys = phys;
    d->size = size;
    d->nregs = 1;
    d->phys2 = 0;
    d->size2 = 0;
    d->irq = irq;
    /* Attempt to capture a second reg entry if present */
    if (path) {
        uint64_t p2 = 0, s2 = 0;
        if (FdtGetRegPa(path, 1, &p2, &s2)) {
            d->phys2 = p2;
            d->size2 = s2;
            d->nregs = 2;
        }
    }
    g_boot_info.count++;
}

void BootInfoInitFromFdt(void)
{

    /* dtb subsystem must already be initialized. */
    uint32_t count = FdtDeviceCount();
    if (count == 0)
        return;

    FdtDevice *arr = (FdtDevice *)KCalloc(count, sizeof(FdtDevice));
    if (!arr)
        return;

    g_boot_info.devs = arr;
    g_boot_info.count = 0;

    /* copy model and cpu strings */
    const char *m = FdtModel();
    if (m && m[0]) {
        g_boot_info.model = (char *)KMalloc(strlen(m) + 1);
        if (g_boot_info.model)
            strcpy(g_boot_info.model, m);
    }
    const char *c = FdtCpuCompat();
    if (c && c[0]) {
        g_boot_info.cpu_compat = (char *)KMalloc(strlen(c) + 1);
        if (g_boot_info.cpu_compat)
            strcpy(g_boot_info.cpu_compat, c);
    }
    FdtEnumerateDevices(collect_dev_cb);
    /* if fewer devices than predicted, we leave the array sized to count */

    uint64_t initrd_start = 0, initrd_end = 0;
    if (FdtGetInitrd(&initrd_start, &initrd_end)) {
        g_boot_info.initrd_pa = initrd_start;
        g_boot_info.initrd_size = initrd_end - initrd_start;
        g_boot_info.has_initrd = true;
    }

    /* Shutdown DTB access to prevent any future libfdt reads */
    FdtShutdown();
}

const char *boot_info_model(void) { return g_boot_info.model ? g_boot_info.model : FdtModel(); }

const char *boot_info_cpu_compat(void)
{
    return g_boot_info.cpu_compat ? g_boot_info.cpu_compat : FdtCpuCompat();
}

void BootInfoEnumerateDevs(void (*cb)(const char *, uint64_t, uint64_t, uint32_t))
{
    if (!cb)
        return;
    for (uint32_t i = 0; i < g_boot_info.count; i++) {
        FdtDevice *d = &g_boot_info.devs[i];
        cb(d->compatible, d->phys, d->size, d->irq);
    }
}

uint32_t boot_info_dev_count(void) { return g_boot_info.count; }

bool boot_info_initrd(uint64_t *out_pa, uint64_t *out_size)
{
    if (!g_boot_info.has_initrd || !out_pa || !out_size)
        return false;
    *out_pa = g_boot_info.initrd_pa;
    *out_size = g_boot_info.initrd_size;
    return true;
}

const FdtDevice *boot_info_dev_array(void) { return (const FdtDevice *)g_boot_info.devs; }

const FdtDevice *boot_info_find_compatible(const char *const *compat)
{
    const FdtDevice *arr = boot_info_dev_array();
    uint32_t cnt = boot_info_dev_count();
    for (uint32_t i = 0; i < cnt; i++) {
        for (const char *const *cp = compat; *cp; cp++) {
            if (strcmp(arr[i].compatible, *cp) == 0)
                return &arr[i];
        }
    }
    return NULL;
}

void BootInfoInit(void)
{
    g_bootinfo_pa = PmmAllocFramesContig((sizeof(BootInfo) + PAGE_SIZE - 1) / PAGE_SIZE);
    BootInfo *bi = (BootInfo *)PA_TO_VA(g_bootinfo_pa);
    memset(bi, 0, sizeof(*bi));
    bi->magic = 0xB007DA7A;

    strncpy(bi->model, boot_info_model(), sizeof(bi->model) - 1);
    strncpy(bi->cpu_compat, boot_info_cpu_compat(), sizeof(bi->cpu_compat) - 1);
    bi->initrd_pa = g_boot_info.initrd_pa;
    bi->initrd_size = g_boot_info.initrd_size;

    /* FdtDevice mirrors BootInfoDevEntry field-for-field, so copy it
     * straight through with no filtering/renaming (unlike Syspage's dev_cb,
     * which is cosmetic-only and must not carry physical addresses). */
    uint32_t count = g_boot_info.count;
    if (count > BOOTINFO_MAX_DEVICES)
        count = BOOTINFO_MAX_DEVICES;
    memcpy(bi->devs, g_boot_info.devs, count * sizeof(BootInfoDevEntry));
    bi->dev_count = count;
}

PhysAddr BootInfoPa(void) { return g_bootinfo_pa; }
