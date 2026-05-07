#ifndef ZUZU_DEVICES_H
#define ZUZU_DEVICES_H

#include <stdint.h>
#include <zuzu/types.h>

typedef struct {
    handle_t dev_handle;
    void *mmio;
} device_t;

device_t device_acquire(uint32_t dev_class, int32_t irq_port);

#endif