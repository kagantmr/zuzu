/**
 * @file syspage.c
 * @brief Builds and updates the system information page.
 */

#include "syspage.h"
#include "boot_info.h"
#include "core/version.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <arch/timer.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/syspage.h>

/** @brief Kernel mapping of the syspage; NULL before SyspageInit(). */
Syspage *g_sp;
/** @brief Physical address of the syspage. */
static PhysAddr g_syspage_pa;

/**
 * @brief BootInfoEnumerateDevs() callback that records a device name in the syspage.
 *
 * Only compatible strings with an "arm," or "smsc," vendor prefix are kept, minus Cortex CPU entries and duplicates. The name is upper-cased.
 *
 * @param[in] compatible The device's compatible string.
 * @param[in] phys Unused.
 * @param[in] size Unused.
 * @param[in] irq Unused.
 */
static void DeviceCallback(const char *compatible, uint64_t phys, uint64_t size, uint32_t irq)
{
    (void)phys;
    (void)size;
    (void)irq;
    if (g_sp->dev_count >= SYSPAGE_MAX_DEVICES)
        return;

    const char *src;
    if (strncmp(compatible, "arm,", 4) == 0)
        src = compatible + 4;
    else if (strncmp(compatible, "smsc,", 5) == 0)
        src = compatible + 5;
    else
        return;

    if (strncmp(src, "cortex", 6) == 0)
        return;

    char name[SYSPAGE_DEV_NAME_LEN];
    int i = 0;
    while (src[i] && i < SYSPAGE_DEV_NAME_LEN - 1) {
        name[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
        i++;
    }
    name[i] = '\0';

    for (uint8_t j = 0; j < g_sp->dev_count; j++)
        if (strncmp(g_sp->devs[j].name, name, SYSPAGE_DEV_NAME_LEN) == 0)
            return;

    strncpy(g_sp->devs[g_sp->dev_count].name, name, SYSPAGE_DEV_NAME_LEN - 1);
    g_sp->dev_count++;
}

void SyspageInit(void)
{
    g_syspage_pa = PmmAllocFrame();
    g_sp = (Syspage *)PA_TO_VA(g_syspage_pa);
    memset(g_sp, 0, sizeof(Syspage));
    g_sp->magic = SYSPAGE_MAGIC;
    g_sp->kernel_ver = (ZUZU_VERSION_MAJOR << 16) | (ZUZU_VERSION_MINOR << 8) | ZUZU_VERSION_PATCH;

    strncpy(g_sp->version, "zuzu-" ZUZU_CODENAME "-" ZUZU_VERSION, sizeof(g_sp->version));

    (void)snprintf(g_sp->build, sizeof(g_sp->build), "%s %s", __DATE__, __TIME__);

    strncpy(g_sp->machine, boot_info_model(), sizeof(g_sp->machine) - 1);
    strncpy(g_sp->cpu, boot_info_cpu_compat(), sizeof(g_sp->cpu) - 1);

    g_sp->mem_total_kb = (uint32_t)((PmmGetStats().total_frames * (uint64_t)PAGE_SIZE) / 1024);
    g_sp->counter_freq = ArchTimerFreq();
    g_sp->counter_boot = ArchTimerNow();

    BootInfoEnumerateDevs(DeviceCallback);

    SyspageUpdateMem();
}
PhysAddr SyspagePa(void) { return g_syspage_pa; }

void SyspageUpdateMem(void)
{
    if (!g_sp)
        return;
    g_sp->mem_free_kb = (uint32_t)((PmmGetStats().free_frames * (uint64_t)PAGE_SIZE) / 1024);
}
