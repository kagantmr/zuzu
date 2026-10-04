#ifndef ZUZU_BOOTINFO_H
#define ZUZU_BOOTINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* rootsvc-only boot device table. Unlike Syspage (cosmetic-only, mapped into
 * every Space), this carries physical addresses and is only ever mapped into
 * the root/init process. Do not map it anywhere else. */

typedef struct {
    char compatible[64];
    uint64_t phys;
    uint64_t size;
    uint64_t phys2;
    uint64_t size2;
    uint32_t irq;
    uint32_t nregs;
} BootInfoDevEntry; /* mirrors kernel/dev/fdt_wrappers.h's FdtDevice layout exactly */

#define BOOTINFO_MAX_DEVICES 120

typedef struct {
    uint32_t magic; /* Must be 0xB007DA7A */
    char model[64];
    char cpu_compat[64];
    uint64_t initrd_pa;
    uint64_t initrd_size;
    uint32_t dev_count;
    BootInfoDevEntry devs[BOOTINFO_MAX_DEVICES];
} BootInfo;

#ifdef __cplusplus
}
#endif

#endif
