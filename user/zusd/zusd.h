#ifndef ZUSD_H
#define ZUSD_H
#include <stdint.h>

typedef struct {
    volatile uint32_t POWER;       // 0x000
    volatile uint32_t CLOCK;       // 0x004
    volatile uint32_t ARGUMENT;    // 0x008
    volatile uint32_t COMMAND;     // 0x00C
    volatile uint32_t RESPCMD;     // 0x010
    volatile uint32_t RESPONSE[4]; // 0x014-0x020
    volatile uint32_t DATATIMER;   // 0x024
    volatile uint32_t DATALENGTH;  // 0x028
    volatile uint32_t DATACTRL;    // 0x02C
    volatile uint32_t DATACNT;     // 0x030  (read-only)
    volatile uint32_t STATUS;      // 0x034
    volatile uint32_t CLEAR;       // 0x038
    volatile uint32_t MASK[2];     // 0x03C-0x040
    uint32_t _pad0[2];             // 0x044-0x048
    volatile uint32_t FIFOCNT;     // 0x04C
    uint32_t _pad1[12];            // 0x050-0x07C
    volatile uint32_t FIFO;        // 0x080
    uint32_t _pad2[983];           // 0x084-0xFDC
    volatile uint32_t PERIPHID[4]; // 0xFE0-0xFEC
    volatile uint32_t PCELLID[4];  // 0xFF0-0xFFC
} pl181_t;

/* STATUS / MASK bits */
#define MCI_CMDCRCFAIL   (1u << 0)
#define MCI_DATACRCFAIL  (1u << 1)
#define MCI_CMDTIMEOUT   (1u << 2)
#define MCI_DATATIMEOUT  (1u << 3)
#define MCI_TXUNDERRUN   (1u << 4)
#define MCI_RXOVERRUN    (1u << 5)
#define MCI_CMDRESPEND   (1u << 6)
#define MCI_CMDSENT      (1u << 7)
#define MCI_DATAEND      (1u << 8)
#define MCI_DATABLOCKEND (1u << 10)
#define MCI_RXDATAAVLBL  (1u << 21)
#define MCI_TXFIFOEMPTY  (1u << 18)

/* COMMAND register flags */
#define MCI_CMD_ENABLE   (1u << 10)
#define MCI_CMD_RESPONSE (1u << 6)
#define MCI_CMD_LONGRESP (1u << 7)

/* POWER register */
#define MCI_POWER_OFF       0x00u
#define MCI_POWER_UP        0x02u
#define MCI_POWER_ON        0x03u
#define MCI_POWER_OPENDRAIN (1u << 6)

/*
 * DATACTRL: bits[7:4]=blocksize(log2), bit[1]=dir(1=read from card), bit[0]=enable
 * 512-byte block = 2^9
 */
#define MCI_DATACTRL_READ  ((9u << 4) | (1u << 1) | (1u << 0))
#define MCI_DATACTRL_WRITE ((9u << 4) | (0u << 1) | (1u << 0))

#define MCI_BLOCK_SIZE  512u
#define MCI_BLOCK_WORDS (MCI_BLOCK_SIZE / 4u)

#endif