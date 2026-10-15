#ifndef ZUZU_BOOTINFO_H
#define ZUZU_BOOTINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    char compatible[64];
    uint64_t phys;
    uint64_t size;
    uint64_t phys2;
    uint64_t size2;
    uint32_t irq;
    uint32_t nregs;
} BootInfoDevEntry;

#define BOOTINFO_MAX_DEVICES 120

typedef struct {
    uint32_t magic;
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
