#ifndef ZUZU_DEV_ENUM_H
#define ZUZU_DEV_ENUM_H

#include <stdint.h>

typedef struct {
    uint32_t id;
    uint32_t phys_base;
    uint32_t size;
    uint32_t irq;
    char compatible[32];
} zuzu_devinfo_t;

#define ZUZU_ENUMDEV_DONE 0xFFFFFFFFu

#endif