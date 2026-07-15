#ifndef PL011DRV_H
#define PL011DRV_H

#define PL011DRV_VER "v2.0"

#include <zuzu/zuzu.h>
#include <stdbool.h>

// pl011drv is based around the pl011 hardware

// ------------------- Constants -------------------

#define PL011DRV_INIT_OK 0
#define PL011DRV_INIT_FAIL -1

// ------------------- PL011 constants -------------------
typedef struct {
    uint32_t DR;        // 0x00
    uint32_t RSR;       // 0x04
    uint32_t _res0[4];  // 0x08-0x14
    uint32_t FR;        // 0x18
    uint32_t _res1;     // 0x1C
    uint32_t ILPR;      // 0x20
    uint32_t IBRD;      // 0x24
    uint32_t FBRD;      // 0x28
    uint32_t LCRH;      // 0x2C
    uint32_t CR;        // 0x30
    uint32_t IFLS;      // 0x34
    uint32_t IMSC;      // 0x38
    uint32_t RIS;       // 0x3C
    uint32_t MIS;       // 0x40
    uint32_t ICR;       // 0x44
} pl011_t;

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

// ------------------- Ringbuffer -------------------

#define UART_RINGBUF_MAX 1024

#include <ring.h>
#include <zuzu/channel.h>

int pl011drv_setup(void);

/* PL011DRV lmsg API: send/receive via the per-thread lmsg buffer */
static inline int32_t pl011drv_write(int32_t port, uint32_t len) {
    return chan_send((handle_t)port, lmsg_buf(), len);
}

static inline msg_t pl011drv_read(int32_t port, uint32_t max_len) {
    return zuzu_msg_lcall(port, max_len);
}

#endif
