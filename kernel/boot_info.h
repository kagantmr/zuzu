#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/dev/fdt_wrappers.h"

/* Simple cached boot info populated once during early boot. */
typedef struct {
    uint32_t count;
    FdtDevice *devs; /* allocated with kmalloc */
    char *model;
    char *cpu_compat;
    uint64_t initrd_pa;
    uint64_t initrd_size;
    bool has_initrd;
} boot_info_t;

/* Initialize boot info from an already-initialized DTB base. */
void boot_info_init_from_dtb();

/* Accessors */
const char *boot_info_model(void);
const char *boot_info_cpu_compat(void);
void boot_info_foreach_dev(void (*cb)(const char *, uint64_t, uint64_t, uint32_t));
uint32_t boot_info_dev_count(void);

/* Bootloader-supplied initrd from the DTB's /chosen node, if present. */
bool boot_info_initrd(uint64_t *out_pa, uint64_t *out_size);

/* Returns pointer to internal array (read-only) */
const FdtDevice *boot_info_dev_array(void);

#endif
