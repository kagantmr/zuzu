#ifndef EMMC2DRV_H
#define EMMC2DRV_H
#include <stdint.h>

typedef struct __attribute__((packed))
{
    volatile uint32_t sdma_addr;        // 0x000
    volatile uint32_t block_size_count; // 0x004
    volatile uint32_t argument;         // 0x008
    volatile uint32_t cmd_xfer_mode;    // 0x00C
    volatile uint32_t response[4];      // 0x010-0x01C
    volatile uint32_t data_port;        // 0x020
    volatile uint32_t present_state;    // 0x024
    volatile uint32_t host_ctrl_1;      // 0x028
    volatile uint32_t clk_ctrl_reset;   // 0x02C
    volatile uint32_t int_status;       // 0x030
    volatile uint32_t int_enable;       // 0x034
    volatile uint32_t int_signal;       // 0x038
    volatile uint32_t host_ctrl_2;      // 0x03C
    volatile uint32_t capabilites[2];   // 0x040-0x44
} Emmc2MMIO;

/* Present State Flags (PRESENT_STATE) */
#define SDHCI_CMD_INHIBIT        (1u << 0)
#define SDHCI_DATA_INHIBIT       (1u << 1)
#define SDHCI_DAT_LINE_ACTIVE    (1u << 2)

/* Transfer Mode Flags (CMD_XFER_MODE lower 16 bits) */
#define SDHCI_TRNS_DMA           (1u << 0)
#define SDHCI_TRNS_BLK_CNT_EN    (1u << 1)
#define SDHCI_TRNS_READ          (1u << 4) 
#define SDHCI_TRNS_MULTI         (1u << 5)

/* Command Flags (CMD_XFER_MODE upper 16 bits) */
#define SDHCI_CMD_RESP_NONE      (0u << 16)
#define SDHCI_CMD_RESP_136       (1u << 16)
#define SDHCI_CMD_RESP_48        (2u << 16)
#define SDHCI_CMD_RESP_48_BUSY   (3u << 16)
#define SDHCI_CMD_CRC_CHECK      (1u << 19)
#define SDHCI_CMD_IDX_CHECK      (1u << 20)
#define SDHCI_CMD_DATA_EN        (1u << 21)
#define SDHCI_CMD_INDEX(x)       (((x) & 0x3F) << 24)

/* Interrupt Status/Enable Flags (INT_STATUS, INT_ENABLE, INT_SIGNAL) */
#define SDHCI_INT_CMD_COMPLETE   (1u << 0)
#define SDHCI_INT_XFER_COMPLETE  (1u << 1)
#define SDHCI_INT_BUF_WR_READY   (1u << 4)
#define SDHCI_INT_BUF_RD_READY   (1u << 5)
#define SDHCI_INT_ERROR          (1u << 15)

/* Clock Control & Resets (CLK_CTRL_RESET) */
#define SDHCI_CLK_INT_EN         (1u << 0)
#define SDHCI_CLK_STABLE         (1u << 1)
#define SDHCI_CLK_SD_EN          (1u << 2)
#define SDHCI_RESET_ALL          (1u << 24)
#define SDHCI_RESET_CMD          (1u << 25)
#define SDHCI_RESET_DATA         (1u << 26)

/* Host Control 1 Power Settings (HOST_CTRL_1) */
#define SDHCI_POWER_ON           (1u << 8)
#define SDHCI_POWER_330V         (7u << 9)

#define MCI_BLOCK_SIZE  512u
#define MCI_BLOCK_WORDS (MCI_BLOCK_SIZE / 4u)

#endif /* EMMC2DRV_H */
