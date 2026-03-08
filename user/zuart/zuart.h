#ifndef ZUART_H
#define ZUART_H

#define ZUART_VER "v1.0"

#include "zuzu.h"
#include <stdbool.h>

// zuart is completely based around the pl011 for now. subject to change

// ------------------- vexpress-a15 constants -------------------

// TEMPORARY !!!!!!!! CHANGE WHEN DTBCLIMBER IS HERE!!!!!!! OR ELSE NO PORTING
#define VEXPRESS_UART0_PA 0x1C090000
#define UART0_IRQ_NUM 37

// --------------------------------------------------------------

// ------------------- IPC constants -------------------

typedef struct {
    uint32_t pid;
    int32_t shmem_handle;
    size_t length;
} zuart_waiting_t;

// -----------------------------------------------------

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

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

#define LCRH_FEN (1u << 4)
#define LCRH_WLEN_8 (3u << 5)

#define CR_UARTEN (1u << 0)
#define CR_TXE (1u << 8)
#define CR_RXE (1u << 9)
// -------------------------------------------------------

// ------------------- Ringbuffer -------------------

#define UART_RINGBUF_MAX 1024

typedef struct ringbuf
{
    char buf[UART_RINGBUF_MAX];
    uint16_t head, tail;
} ringbuf_t;

static inline bool rb_full(ringbuf_t *rb)
{
    return (rb->head + 1) % UART_RINGBUF_MAX == rb->tail;
}

static inline bool rb_empty(ringbuf_t *rb)
{
    return (rb->head) == rb->tail;
}

static inline bool rb_write(ringbuf_t *rb, char ch)
{
    if (rb_full(rb))
        return false;
    rb->buf[rb->head] = ch;
    rb->head = (rb->head + 1) % UART_RINGBUF_MAX;
    return true;
}

static inline char rb_read(ringbuf_t *rb)
{
    char c = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % UART_RINGBUF_MAX;
    return c;
}


// --------------------------------------------------

#endif
