#ifndef ZUZU_DEVICES_H
#define ZUZU_DEVICES_H

#include <zuzu/syspage.h>
#include <string.h>

static inline const syspage_dev_t *device_find(const char *name) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    for (uint8_t i = 0; i < sp->dev_count; i++)
        if (strncmp(sp->devs[i].name, name, SYSPAGE_DEV_NAME_LEN) == 0)
            return &sp->devs[i];
    return NULL;
}

static inline int device_present(const char *name) {
    return device_find(name) != NULL;
}

#endif