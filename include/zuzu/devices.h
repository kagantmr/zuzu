#ifndef ZUZU_DEVICES_H
#define ZUZU_DEVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zuzu/syspage.h>
#include <string.h>

/**
 * @brief Finds a device by name in the system page.
 * 
 * @param name The name of the device to find.
 * 
 * @return const syspage_dev_t* Returns a pointer to the device structure if found, or NULL if not found.
 */
static inline const syspage_dev_t *device_find(const char *name) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    for (uint8_t i = 0; i < sp->dev_count; i++)
        if (strncmp(sp->devs[i].name, name, SYSPAGE_DEV_NAME_LEN) == 0)
            return &sp->devs[i];
    return NULL;
}

/**
 * @brief Checks if a device with the specified name is present in the system page.
 * 
 * @param name The name of the device to check for.
 * 
 * @return int Returns 1 if the device is present, 0 otherwise.
 */
static inline int device_present(const char *name) {
    return device_find(name) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif