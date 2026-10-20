/**
 * @file syspage.h
 * @brief System page memory object.
 */

#ifndef SYSPAGE_H
#define SYSPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stddef.h"
#include "stdint.h"
#include "types.h"

/**
 * @brief Syspage magic value.
 */
#define SYSPAGE_MAGIC 0x50050CA7

/**
 * @brief The maximum number of devices that could fit into the syspage.
 */
#define SYSPAGE_MAX_DEVICES 120

/**
 * @brief Maximum length for a device name.
 */
#define SYSPAGE_DEV_NAME_LEN 32

/**
 * @brief Fixed virtual address of the syspage.
 */
#define SYSPAGE_VA 0x1000

/**
 * @brief Pointer for the syspage.
 */
#define SYSPAGE (void *)SYSPAGE_VA

/**
 * @brief Struct representing a syspage device entry.
 */
typedef struct {
    char name[SYSPAGE_DEV_NAME_LEN]; /**< Name of the device. */
} SyspageDevEntry;

/**
 * @brief Struct representing the syspage's layout.
 */
typedef struct {
    uint32_t magic;      /**< Syspage magic value. Must be 0x50050CA7 "zoozoo cat". */
    uint32_t kernel_ver; /**< zuzu kernel version. Encoded as 0x00MMmmpp, where M: Major, m: minor,
                            p: patch */
    char version[24];    /**<  zuzu version string. Something in the form of zuzu-v2.0-prowl */
    char build[24];      /**< Build date. */
    char machine[20];    /**< Machine zuzu is booting on. */
    char cpu[24];        /**< CPU within the machine. */
    size_t mem_total_kb; /**< Total memory in kilobytes. */
    size_t mem_free_kb;  /**< Free memory in kilobytes. */
    uint64_t counter_freq;                     /**< Counter frequency.  */
    uint64_t counter_boot;                     /**< Counter value at boot. */
    uint8_t dev_count;                         /**< Device count. */
    uint32_t _reserved;                        /**< Reserved */
    SyspageDevEntry devs[SYSPAGE_MAX_DEVICES]; /**< Device names list. */
    uint32_t _reserved2;                       /**< Reserved */
} Syspage;

_Static_assert(offsetof(Syspage, _reserved2) == 3976, "syspage ABI");
_Static_assert(sizeof(Syspage) <= 4096, "Syspage must fit in one page");

#ifdef __cplusplus
}
#endif

#endif
