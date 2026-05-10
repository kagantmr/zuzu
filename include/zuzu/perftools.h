#ifndef ARCH_PERFTOOLS_H
#define ARCH_PERFTOOLS_H

#include <stdint.h>

static inline uint32_t read_cycle_counter()
{
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(value));
    return value;
}

#endif