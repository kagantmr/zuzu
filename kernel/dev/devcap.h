#ifndef KERNEL_DEVICE_CAP_H
#define KERNEL_DEVICE_CAP_H

#include <zuzu/types.h>

typedef struct {
    PhysAddr phys_base;
    size_t size;
    char compatible[32]; // DTB compatible string
    Irq irq;
    size_t ref_count;
} DeviceCap;

#endif