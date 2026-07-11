#ifndef LAN9118_REGS_H
#define LAN9118_REGS_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t rx_data_fifo_port;           // 0x00
    uint32_t rx_data_fifo_alias_ports[7]; // 0x04-0x1C
    uint32_t tx_data_fifo_port;           // 0x20
    uint32_t tx_data_fifo_alias_ports[7]; // 0x24-0x3C
    uint32_t rx_status_fifo_port;         // 0x40
    uint32_t rx_status_fifo_peek;         // 0x44
    uint32_t tx_status_fifo_port;         // 0x48
    uint32_t tx_status_fifo_peek;         // 0x4C
    uint32_t id_rev;                      // 0x50
    uint32_t irq_cfg;                     // 0x54
    uint32_t int_sts;                     // 0x58
    uint32_t int_en;                      // 0x5C
    uint32_t _reserved;
    uint32_t byte_test;   // 0x64
    uint32_t fifo_int;    // 0x68
    uint32_t rx_cfg;      // 0x6C
    uint32_t tx_cfg;      // 0x70
    uint32_t hw_cfg;      // 0x74
    uint32_t rx_dp_ctl;   // 0x78
    uint32_t rx_fifo_inf; // 0x7C
    uint32_t tx_fifo_inf; // 0x80
    uint32_t pmt_ctrl;    // 0x84
    uint32_t gpio_cfg;    // 0x88
    uint32_t gpt_cfg;     // 0x8C
    uint32_t gpt_cnt;     // 0x90
    uint32_t _reserved2;
    uint32_t word_swap;      // 0x98
    uint32_t free_run;       // 0x9C
    uint32_t rx_drop;        // 0xA0
    uint32_t mac_csr_cmd;    // 0xA4
    uint32_t mac_csr_data;   // 0xA8
    uint32_t afc_cfg;        // 0xAC
    uint32_t e2p_cmd;        // 0xB0
    uint32_t e2p_data;       // 0xB4
    uint32_t _reserved3[17]; // 0xB8-0xFC
} lan9118_t;

#define HW_CFG_SRST (1u << 0)
#define HW_CFG_SRST_TO (1u << 1)
#define HW_CFG_MBO (1u << 20)
#define HW_CFG_TX_FIF_SZ_SHIFT 16
#define HW_CFG_TX_FIF_SZ_MASK (0xF << 16)

#define MAC_CSR_CMD_BUSY (1u << 31)
#define MAC_CSR_CMD_RNW (1u << 30)
#define MAC_CR_TXEN (1u << 3)
#define MAC_CR_RXEN (1u << 2)
#define MAC_CSR_CMD_ADDR_MASK 0xFF
#define MAC_CSR_MAC_CR 1
#define MAC_CSR_ADDRH 2
#define MAC_CSR_ADDRL 3

#define BYTE_TEST_VALUE 0x87654321u

#define TX_CFG_TX_ON (1u << 1)
#define TX_CFG_STOP_TX (1u << 0)
#define TX_CFG_TXS_DUMP (1u << 15)
#define TX_CFG_TXD_DUMP (1u << 14)

#define RX_CFG_RX_DUMP (1u << 15)

#define INT_RSFL (1u << 3)  // RX status FIFO level: packet arrived
#define INT_RSFF (1u << 4)  // RX status FIFO full
#define INT_RXDF (1u << 6)  // RX dropped frame
#define INT_TSFL (1u << 7)  // TX status FIFO level: TX complete
#define INT_TSFF (1u << 8)  // TX status FIFO full
#define INT_TDFA (1u << 9)  // TX data FIFO available
#define INT_TDFO (1u << 10) // TX data FIFO overrun
#define INT_RXE (1u << 14)  // receiver error
#define INT_TXE (1u << 13)  // transmitter error
#define INT_PHY (1u << 18)  // PHY interrupt

// IRQ_CFG bits
#define IRQ_CFG_IRQ_EN   (1u << 8)   // master interrupt output enable
#define IRQ_CFG_IRQ_POL  (1u << 4)   // 1 = active-high
#define IRQ_CFG_IRQ_TYPE (1u << 0)   // 1 = push-pull (not open-drain)

_Static_assert(offsetof(lan9118_t, tx_data_fifo_port) == 0x20, "tx_data_fifo_port offset wrong");
_Static_assert(offsetof(lan9118_t, byte_test) == 0x64, "byte_test offset wrong");
_Static_assert(offsetof(lan9118_t, hw_cfg) == 0x74, "hw_cfg offset wrong");
_Static_assert(offsetof(lan9118_t, mac_csr_cmd) == 0xA4, "mac_csr_cmd offset wrong");

#endif /* LAN9118_REGS_H */