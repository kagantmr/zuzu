#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include "kernel/dev/fdt_wrappers.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

typedef struct {
    uint32_t count;
    FdtDevice *devs;
    char *model;
    char *cpu_compat;
    uint64_t initrd_pa;
    uint64_t initrd_size;
    bool has_initrd;
} KernelBootInfo;

void BootInfoInitFromFdt(void);

const char *boot_info_model(void);
const char *boot_info_cpu_compat(void);
void BootInfoEnumerateDevs(void (*cb)(const char *, uint64_t, uint64_t, uint32_t));
uint32_t boot_info_dev_count(void);

bool boot_info_initrd(uint64_t *out_pa, uint64_t *out_size);

const FdtDevice *boot_info_dev_array(void);

const FdtDevice *boot_info_find_compatible(const char *const *compat);

void BootInfoInit(void);
PhysAddr BootInfoPa(void);

#endif
