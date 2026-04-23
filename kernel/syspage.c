#include "syspage.h"
#include <zuzu/syspage.h>
#include "kernel/mm/pmm.h"
#include <string.h>
#include <stdio.h>
#include "kernel/time/tick.h"
#include "kernel/mm/vmm.h"
#include "core/version.h"
#include "kernel/dtb/dtb.h"

zuzu_syspage_t *g_sp;
static uintptr_t g_syspage_pa;
extern pmm_state_t pmm_state;

static void dev_cb(const char *compatible, uint64_t phys, uint64_t size, uint32_t irq)
{
    (void)phys; (void)size; (void)irq;
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

    /* build the display name */
    char name[SYSPAGE_DEV_NAME_LEN];
    int i = 0;
    while (src[i] && i < SYSPAGE_DEV_NAME_LEN - 1) {
        name[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
        i++;
    }
    name[i] = '\0';

    /* skip duplicates */
    for (uint8_t j = 0; j < g_sp->dev_count; j++)
        if (strncmp(g_sp->devs[j].name, name, SYSPAGE_DEV_NAME_LEN) == 0)
            return;

    strncpy(g_sp->devs[g_sp->dev_count].name, name, SYSPAGE_DEV_NAME_LEN - 1);
    g_sp->dev_count++;
}

void syspage_init(void)
{
    g_syspage_pa = pmm_alloc_page(); // reserve the first page for the syspage
    g_sp = (zuzu_syspage_t *)PA_TO_VA(g_syspage_pa);
    memset(g_sp, 0, sizeof(zuzu_syspage_t));
    g_sp->magic = 0x50050CA7;

    strncpy(g_sp->version, ZUZU_VERSION, sizeof(g_sp->version));

    snprintf(g_sp->build, sizeof(g_sp->build), "%s %s", __DATE__, __TIME__);

    char machine[20] = "Unknown";
    dtb_get_string("/", "model", machine, sizeof(machine));
    strncpy(g_sp->machine, machine, sizeof(g_sp->machine) - 1);

    char cpu[24] = "Unknown";
    dtb_get_string("/cpus/cpu@0", "compatible", cpu, sizeof(cpu));
    strncpy(g_sp->cpu, cpu, sizeof(g_sp->cpu) - 1);

    g_sp->mem_total_kb = (uint32_t)((pmm_state.total_pages * (uint64_t)PAGE_SIZE) / 1024);

    dtb_enum_devices(dev_cb);

    syspage_update_mem();

    // g_sp->mem_free_kb = (uint32_t)((pmm_state.free_pages  * (uint64_t)PAGE_SIZE) / 1024);
}
uintptr_t syspage_pa(void)
{
    return g_syspage_pa;
}
void syspage_update_mem(void)
{
    if (!g_sp)
        return;
    g_sp->mem_free_kb = (uint32_t)((pmm_state.free_pages * (uint64_t)PAGE_SIZE) / 1024);
}
void syspage_update_uptime(void)
{
    if (!g_sp)
        return;
    g_sp->uptime_s = (uint32_t)(get_ticks() / TICK_HZ);
}