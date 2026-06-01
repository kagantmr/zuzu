#ifndef KERNEL_DTB_H
#define KERNEL_DTB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char     compatible[64];
    uint64_t phys;
    uint64_t size;
    uint64_t phys2;
    uint64_t size2;
    uint32_t irq;
    uint32_t nregs;
} dtb_dev_t;

bool     dtb_init(const void *dtb_base);

/* Enumerate devices in the DTB without storing a global static table. */
void     dtb_enum_devices(void (*cb)(const char * /* compatible */, const char * /* path */, uint64_t /* phys */, uint64_t /* size */, uint32_t /* irq */));
uint32_t dtb_count_devices(void);

/* Query helpers (intended for early boot / arch use). */
bool dtb_get_reg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);
bool dtb_get_reg_phys(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);
bool dtb_find_compatible(const char *compatible, char *out_path, size_t out_path_cap);

/* Simple string queries */
const char *dtb_model(void);
const char *dtb_cpu_compat(void);

/* Arch hooks: arch code can provide stronger implementations by defining
   these symbols (they are weakly referenced in dtb.c). */
bool dtb_translate_address_arch(const char *node_path, uint64_t raw_addr, uint64_t *out_phys);
bool dtb_resolve_irq_arch(const char *node_path, uint32_t child_irq, uint32_t *out_irq, uint32_t *out_flags);

/* Shutdown DTB access: clears internal pointer so libfdt won't be used further */
void dtb_shutdown(void);


#endif
