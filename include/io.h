#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline uint32_t readl(uintptr_t addr) {
    return *(volatile uint32_t*)addr;
}
static inline void writel(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

static inline uint8_t readb(uintptr_t addr) {
    return *(volatile uint8_t*)addr;
}

static inline void writeb(uintptr_t addr, uint8_t value){
    *(volatile uint8_t*)addr = value;
}

#endif // IO_H