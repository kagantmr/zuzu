/**
 * @file
 * @brief Look-ups in the syspage device list.
 */

#ifndef ZUZU_DEVICES_H
#define ZUZU_DEVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <zuzu/syspage.h>

/**
 * @brief Finds a device by its display name in the syspage device list.
 *
 * @param[in] name Display name, as listed in the syspage (e.g. "PL111 CLCD").
 * @return The entry, or NULL if no device has that name.
 */
static inline const SyspageDevEntry *FindDeviceInSyspage(const char *name)
{
    Syspage *sp = (Syspage *)SYSPAGE;
    for (uint8_t i = 0; i < sp->dev_count; i++)
        if (strncmp(sp->devs[i].name, name, SYSPAGE_DEV_NAME_LEN) == 0)
            return &sp->devs[i];
    return NULL;
}

/**
 * @brief Tells whether a device with this display name is in the syspage device list.
 *
 * @param[in] name Display name to look for.
 * @return 1 if present, 0 otherwise.
 */
static inline int DevicePresentInSyspage(const char *name)
{
    return FindDeviceInSyspage(name) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif