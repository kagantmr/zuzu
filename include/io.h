#ifndef IO_H
#define IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Reads a 32-bit value from the specified memory-mapped I/O address.
 */
static inline uint32_t readl(uintptr_t addr) {
    return *(volatile uint32_t*)addr;
}

/**
 * @brief Writes a 32-bit value to the specified memory-mapped I/O address.
 */
static inline void writel(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

/**
 * @brief Reads an 8-bit value from the specified memory-mapped I/O address.
 */
static inline uint8_t readb(uintptr_t addr) {
    return *(volatile uint8_t*)addr;
}

/**
 * @brief Writes an 8-bit value to the specified memory-mapped I/O address.
 */
static inline void writeb(uintptr_t addr, uint8_t value){
    *(volatile uint8_t*)addr = value;
}

#ifdef __cplusplus
}
#endif

#endif // IO_H