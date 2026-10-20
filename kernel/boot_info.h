/**
 * @file boot_info.h
 * @brief Boot information gathered from the device tree, and the BootInfo page handed to userspace.
 */

#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include "kernel/dev/fdt_wrappers.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Boot information copied out of the device tree before it is discarded.
 */
typedef struct {
    uint32_t count; /**< Number of entries filled in @c devs. */
    FdtDevice *devs; /**< Heap array of discovered devices, or NULL. */
    char *model; /**< Heap copy of the machine model string, or NULL. */
    char *cpu_compat; /**< Heap copy of the CPU compatible string, or NULL. */
    uint64_t initrd_pa; /**< Physical address of the initrd. */
    uint64_t initrd_size; /**< Size of the initrd in bytes. */
    bool has_initrd; /**< The device tree named an initrd. */
} KernelBootInfo;

/**
 * @brief Copy the model, CPU, device list and initrd range out of the device tree, then shut the FDT wrappers down.
 *
 * Does nothing if the tree has no devices or the device array cannot be allocated.
 */
void BootInfoInitFromFdt(void);

/**
 * @brief Get the machine model string.
 *
 * @return The saved copy, or the live device tree's model if no copy was made.
 */
const char *boot_info_model(void);
/**
 * @brief Get the CPU compatible string.
 *
 * @return The saved copy, or the live device tree's value if no copy was made.
 */
const char *boot_info_cpu_compat(void);
/**
 * @brief Call a callback for every saved device.
 *
 * @param[in] cb Called with the compatible string, physical base, size and interrupt number of each device. Does nothing if NULL.
 */
void BootInfoEnumerateDevs(void (*cb)(const char *, uint64_t, uint64_t, uint32_t));
/**
 * @brief Get the number of saved devices.
 *
 * @return The device count.
 */
uint32_t boot_info_dev_count(void);

/**
 * @brief Get the initrd location reported by the bootloader.
 *
 * @param[out] out_pa Receives the physical address of the initrd.
 * @param[out] out_size Receives the size of the initrd in bytes.
 * @return true if an initrd was found, false if there is none or an argument is NULL.
 */
bool boot_info_initrd(uint64_t *out_pa, uint64_t *out_size);

/**
 * @brief Get the saved device array.
 *
 * @return The array of boot_info_dev_count() entries, or NULL if none was saved.
 */
const FdtDevice *boot_info_dev_array(void);

/**
 * @brief Find the first saved device whose compatible string matches any in a list.
 *
 * @param[in] compat NULL-terminated array of compatible strings to look for.
 * @return The first matching device, or NULL if none matches.
 */
const FdtDevice *boot_info_find_compatible(const char *const *compat);

/**
 * @brief Build the read-only BootInfo page that is mapped into the root service.
 *
 * Allocates physical frames, fills in the magic, model, CPU, initrd range and device entries (clamped to BOOTINFO_MAX_DEVICES).
 */
void BootInfoInit(void);
/**
 * @brief Get the physical address of the BootInfo page built by BootInfoInit().
 *
 * @return The physical address.
 */
PhysAddr BootInfoPa(void);

#endif
