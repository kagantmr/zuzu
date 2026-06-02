#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/dtb/dtb.h"

/* Simple cached boot info populated once during early boot. */
typedef struct {
    uint32_t count;
    dtb_dev_t *devs; /* allocated with kmalloc */
    char *model;
    char *cpu_compat;
} boot_info_t;

/* Initialize boot info from an already-initialized DTB base. */
void boot_info_init_from_dtb();

/* Accessors */
const char *boot_info_model(void);
const char *boot_info_cpu_compat(void);
void boot_info_foreach_dev(void (*cb)(const char *, uint64_t, uint64_t, uint32_t));
uint32_t boot_info_dev_count(void);

/* Returns pointer to internal array (read-only) */
const dtb_dev_t *boot_info_dev_array(void);

#endif
