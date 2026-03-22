#ifndef ZUSD_H
#define ZUSD_H
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    volatile uint32_t POWER;       // 0x000
    volatile uint32_t CLOCK;       // 0x004
    volatile uint32_t ARGUMENT;    // 0x008
    volatile uint32_t COMMAND;     // 0x00C
    volatile uint32_t RESPCMD;     // 0x010
    volatile uint32_t RESPONSE[4]; // 0x014-0x020
    volatile uint32_t DATATIMER;   // 0x024
    volatile uint32_t DATALENGTH;  // 0x028
    volatile uint32_t DATACTRL;    // 0x02C
    volatile uint32_t DATACNT;     // 0x030
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

#define CMDCRCFAIL (1u << 0)
#define DATACRCFAIL (1u << 1)
#define CMDTIMEOUT (1u << 2)
#define DATATIMEOUT (1u << 3)
#define TXUNDERRUN (1u << 4)
#define RXOVERRUN (1u << 5)
#define CMDSENT (1u << 6)
#define CMDRESPEND (1u << 7)
#define DATAEND (1u << 8)
#define DATABLOCKEND (1u << 10)
#define RXDATAAVLBL (1u << 17)
#define TXFIFOEMPTY (1u << 21)

#define MCI_CMD_ENABLE      (1u << 10)   // must be set to send a command
#define MCI_CMD_RESPONSE    (1u << 6)    // expect a response
#define MCI_CMD_LONGRESP    (1u << 7)    // expect a 136-bit response
#define MCI_CMD_INTERRUPT   (1u << 8)    // disable command timer
#define MCI_CMD_PENDING     (1u << 9)    // wait for CmdPend before sending

#define MCI_POWER_OFF       0x00
#define MCI_POWER_UP        0x02
#define MCI_POWER_ON        0x03
#define MCI_POWER_OPENDRAIN (1u << 6)    // needed during card init
#define MCI_POWER_ROD       (1u << 7)


#endif
