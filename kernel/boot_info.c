#include "boot_info.h"
#include "kernel/dtb/dtb.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include <libfdt.h>
#include <string.h>
#include <stddef.h>

#define LOG_FMT(fmt) "(boot_info) " fmt
#include "core/log.h"

static boot_info_t g_boot_info = {0};

static void collect_dev_cb(const char *compatible, const char *path, uint64_t phys, uint64_t size, uint32_t irq)
{
    if (!g_boot_info.devs)
        return;
    uint32_t idx = g_boot_info.count;
    dtb_dev_t *d = &g_boot_info.devs[idx];
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
        if (dtb_get_reg_phys(path, 1, &p2, &s2)) {
            d->phys2 = p2;
            d->size2 = s2;
            d->nregs = 2;
        }
    }
    g_boot_info.count++;
}

void boot_info_init_from_dtb()
{

    /* dtb subsystem must already be initialized. */
    uint32_t count = dtb_count_devices();
    if (count == 0)
        return;

    dtb_dev_t *arr = (dtb_dev_t *)kmalloc(sizeof(dtb_dev_t) * count);
    if (!arr)
        return;
    memset(arr, 0, sizeof(dtb_dev_t) * count);

    g_boot_info.devs = arr;
    g_boot_info.count = 0;

    /* copy model and cpu strings */
    const char *m = dtb_model();
    if (m && m[0]) {
        g_boot_info.model = (char *)kmalloc(strlen(m) + 1);
        if (g_boot_info.model)
            strcpy(g_boot_info.model, m);
    }
    const char *c = dtb_cpu_compat();
    if (c && c[0]) {
        g_boot_info.cpu_compat = (char *)kmalloc(strlen(c) + 1);
        if (g_boot_info.cpu_compat)
            strcpy(g_boot_info.cpu_compat, c);
    }
    dtb_enum_devices(collect_dev_cb);
    /* if fewer devices than predicted, we leave the array sized to count */
    /* Shutdown DTB access to prevent any future libfdt reads */
    dtb_shutdown();
}

const char *boot_info_model(void)
{
    return g_boot_info.model ? g_boot_info.model : dtb_model();
}

const char *boot_info_cpu_compat(void)
{
    return g_boot_info.cpu_compat ? g_boot_info.cpu_compat : dtb_cpu_compat();
}

void boot_info_foreach_dev(void (*cb)(const char *, uint64_t, uint64_t, uint32_t))
{
    if (!cb)
        return;
    for (uint32_t i = 0; i < g_boot_info.count; i++) {
        dtb_dev_t *d = &g_boot_info.devs[i];
        cb(d->compatible, d->phys, d->size, d->irq);
    }
}

uint32_t boot_info_dev_count(void)
{
    return g_boot_info.count;
}

const dtb_dev_t *boot_info_dev_array(void)
{
    return (const dtb_dev_t *)g_boot_info.devs;
}
