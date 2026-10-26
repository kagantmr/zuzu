#ifndef USER_DRIVERS_PL050DRV_KMI_H
#define USER_DRIVERS_PL050DRV_KMI_H

#include <stdint.h>
/**
 * taken from OSDev
 */
typedef struct {
    uint32_t cr;   // control register (rw)
    uint32_t stat; // status register (r)
    uint32_t data; // data register (rw)
    uint32_t clk;  // clock divisor register (rw)
    uint32_t ir;   // interrupt control register (r)
} Pl050Mmio;

#define KMICR_EN 0x04
#define KMICR_TXINTREN 0x08
#define KMICR_RXINTREN 0x10

#define KMISTAT_RXFULL 0x10
#define KMISTAT_TXEMPTY 0x40
#define KMISTAT_RXPARITY 0x04

#define KMIIR_RXINTR 0x01
#define KMIIR_TXINTR 0x02

#endif /* USER_DRIVERS_PL050DRV_KMI_H */