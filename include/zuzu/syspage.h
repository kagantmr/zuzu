#ifndef SYSPAGE_H
#define SYSPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stddef.h"
#include "stdint.h"
#include "types.h"

#define SYSPAGE_MAX_DEVICES 120
#define SYSPAGE_DEV_NAME_LEN 32
#define SYSPAGE_VA 0x1000

#define SYSPAGE (void *)SYSPAGE_VA

typedef struct {
    char name[SYSPAGE_DEV_NAME_LEN];
} SyspageDevEntry;

typedef struct {
    uint32_t magic;
    uint32_t kernel_ver;
    char version[24];
    char build[24];
    char machine[20];
    char cpu[24];
    size_t mem_total_kb;
    size_t mem_free_kb;
    uint64_t counter_freq;
    uint64_t counter_boot;
    uint8_t dev_count;
    size_t initrd_size;
    SyspageDevEntry devs[SYSPAGE_MAX_DEVICES];
    uint32_t _reserved;
} Syspage;

_Static_assert(offsetof(Syspage, _reserved) == 3976, "syspage ABI");
_Static_assert(sizeof(Syspage) <= 4096, "Syspage must fit in one page");

#ifdef __cplusplus
}
#endif

#endif
