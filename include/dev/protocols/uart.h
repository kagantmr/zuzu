#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>

#define UART_RING_DATA_SIZE 1024u

typedef struct {
    ShmRingHdr tx_hdr; uint8_t tx_data[UART_RING_DATA_SIZE]; /* client to driver */
    ShmRingHdr rx_hdr; uint8_t rx_data[UART_RING_DATA_SIZE]; /* driver to client */
} UartShm;

#define UART_OPEN      1 /* grant: CreateMem(1) page, driver lays out UartShm itself */
#define UART_NOTIFY 3

typedef struct { uint32_t cmd; } UartOpenRequest;
typedef struct { uint32_t cmd; uint32_t bit; } UartNotifyRequest;
typedef struct { Err status; uint32_t bit; } UartOpenReply;

#ifdef __cplusplus
}
#endif

#endif