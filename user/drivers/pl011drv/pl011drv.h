#ifndef PL011DRV_H
#define PL011DRV_H

#define PL011DRV_VER "v3.0"

#include <stdbool.h>
#include <zuzu/zuzu.h>

// pl011drv is based around the pl011 hardware

// ------------------- Constants -------------------

#define PL011DRV_INIT_OK 0
#define PL011DRV_INIT_FAIL -1

// ------------------- PL011 constants -------------------
typedef struct {
    uint32_t dr;      // 0x00
    uint32_t rsr;     // 0x04
    uint32_t res0[4]; // 0x08-0x14
    uint32_t fr;      // 0x18
    uint32_t res1;    // 0x1C
    uint32_t ilpr;    // 0x20
    uint32_t ibrd;    // 0x24
    uint32_t fbrd;    // 0x28
    uint32_t lcrh;    // 0x2C
    uint32_t cr;      // 0x30
    uint32_t ifls;    // 0x34
    uint32_t imsc;    // 0x38
    uint32_t ris;     // 0x3C
    uint32_t mis;     // 0x40
    uint32_t icr;     // 0x44
} Pl011Mmio;

#define IMSC_RXIM (1u << 4) // RX interrupt mask
#define IMSC_TXIM (1u << 5) // TX interrupt mask
#define IMSC_RTIM (1u << 6) // RX timeout interrupt mask

#define ICR_ALL (0x7FFu)

#define IFLS_RX_SHIFT 3
#define IFLS_RX_MASK (7u << IFLS_RX_SHIFT)
#define IFLS_RX_1_8 (0u << IFLS_RX_SHIFT)

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

#define LCRH_FEN (1u << 4)
#define LCRH_WLEN_8 (3u << 5)

#define CR_UARTEN (1u << 0)
#define CR_TXE (1u << 8)
#define CR_RXE (1u << 9)

#endif
