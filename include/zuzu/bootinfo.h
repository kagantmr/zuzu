/**
 * @file bootinfo.h
 * @brief Kernel-passed boot information formats.
 */

#ifndef ZUZU_BOOTINFO_H
#define ZUZU_BOOTINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief One device described by the device tree, as handed to the root service.
 */
typedef struct {
    char compatible[64]; /**< The device's compatible string, NUL-terminated. */
    uint64_t phys;       /**< Physical base address of the first register block. */
    uint64_t size;       /**< Size of the first register block in bytes. */
    uint64_t phys2;      /**< Physical base address of the second register block, or 0 if none. */
    uint64_t size2;      /**< Size of the second register block in bytes, or 0 if none. */
    uint32_t irq;        /**< The interrupt line the device uses. */
    uint32_t nregs;      /**< Number of register blocks recorded: 1 or 2. */
} BootInfoDevEntry;

/**
 * @brief Maximum number of devices recorded in BootInfo; any beyond this are dropped.
 */
#define BOOTINFO_MAX_DEVICES 120

/**
 * @brief Boot information the kernel gathers from the device tree and maps read-only into the
 * root service at USER_BOOTINFO_VA.
 */
typedef struct {
    uint32_t magic;       /**< Set to 0xB007DA7A once the kernel has filled the page in. */
    char model[64];       /**< The board's model string, NUL-terminated. */
    char cpu_compat[64];  /**< The CPU's compatible string, NUL-terminated. */
    uint64_t initrd_pa;   /**< Physical address of the initrd. */
    uint64_t initrd_size; /**< Size of the initrd in bytes. */
    uint32_t dev_count;   /**< Number of valid entries in @c devs. */
    BootInfoDevEntry devs[BOOTINFO_MAX_DEVICES]; /**< The devices found in the device tree. */
} BootInfo;

#ifdef __cplusplus
}
#endif

#endif
