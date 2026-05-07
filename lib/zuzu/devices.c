#include <zuzu/devices.h>
#include <zuzu/umem.h>
#include <string.h>

device_t device_acquire(uint32_t dev_class, int32_t irq_port) {
    (void)dev_class;
    (void)irq_port;
    device_t d;
    d.dev_handle = -1;
    d.mmio = NULL;
    return d;
}
