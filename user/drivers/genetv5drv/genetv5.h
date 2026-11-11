#ifndef USER_DRIVERS_GENETV5DRV_GENETV5DRV_H
#define USER_DRIVERS_GENETV5DRV_GENETV5DRV_H

#include <types.h>

typedef struct {
    volatile uint32_t rev_ctrl;          /* 0x00 */
    volatile uint32_t port_ctrl;         /* 0x04 */
    volatile uint32_t rbuf_flush_ctrl;   /* 0x08 */
    volatile uint32_t tbuf_flush_ctrl;   /* 0x0c */
} GenetSys;
_Static_assert(offsetof(GenetSys, tbuf_flush_ctrl) == 0x0c, "layout");

#define GENET_SYS_OFF 0x0000
#define GENET_EXT_OFF 0x0080
#define GENET_RBUF_OFF 0x0300
#define GENET_UMAC_OFF 0x0800
#define GENET_RDMA_CTRL_OFF 0x3040
#define GENET_TDMA_CTRL_OFF 0x5040

#define PORT_MODE_EXT_GPHY 3
#define RBUF_FLUSH_RESET (1u << 1)

typedef struct {
    volatile uint32_t pwr_mgmt;          /* 0x00 */
    uint32_t _rsvd0[2];
    volatile uint32_t rgmii_oob_ctrl;    /* 0x0c */
} GenetExt;
_Static_assert(offsetof(GenetExt, rgmii_oob_ctrl) == 0x0c, "layout");

#define RGMII_LINK (1u << 4)
#define OOB_DISABLE (1u << 5)
#define RGMII_MODE_EN (1u << 6)
#define ID_MODE_DIS (1u << 16)

typedef struct {
    volatile uint32_t ctrl;              /* 0x00 */
    uint32_t _rsvd0[(0xb4 - 0x04) / 4];
    volatile uint32_t tbuf_size_ctrl;    /* 0xb4 */
} GenetRbuf;
_Static_assert(offsetof(GenetRbuf, tbuf_size_ctrl) == 0xb4, "layout");

#define RBUF_ALIGN_2B (1u << 1)

typedef struct {
    volatile uint32_t unused;          /* 0x000 */
    volatile uint32_t hd_bkp_ctrl;     /* 0x004 */
    volatile uint32_t cmd;             /* 0x008 */
    volatile uint32_t mac0;            /* 0x00c */
    volatile uint32_t mac1;            /* 0x010 */
    volatile uint32_t max_frame_len;   /* 0x014 */
    uint32_t _rsvd0[(0x334 - 0x018) / 4];
    volatile uint32_t tx_flush;        /* 0x334 */
    uint32_t _rsvd1[(0x580 - 0x338) / 4];
    volatile uint32_t mib_ctrl;        /* 0x580 */
    uint32_t _rsvd2[(0x614 - 0x584) / 4];
    volatile uint32_t mdio_cmd;        /* 0x614 */
} GenetUmac;
_Static_assert(offsetof(GenetUmac, tx_flush) == 0x334, "layout");
_Static_assert(offsetof(GenetUmac, mib_ctrl) == 0x580, "layout");
_Static_assert(offsetof(GenetUmac, mdio_cmd) == 0x614, "layout");

#define CMD_TX_EN (1u << 0)
#define CMD_RX_EN (1u << 1)
#define CMD_SPEED_SHIFT 2
#define CMD_SW_RESET (1u << 13)
#define CMD_LCL_LOOP_EN (1u << 15)

#define UMAC_SPEED_10 0
#define UMAC_SPEED_100 1
#define UMAC_SPEED_1000 2

#define MIB_RESET_ALL 0x7u
#define ENET_MAX_FRAME_LEN 1536

typedef struct {
    volatile uint32_t ring_cfg;          /* 0x00 */
    volatile uint32_t ctrl;              /* 0x04 */
    volatile uint32_t status;            /* 0x08 */
    volatile uint32_t scb_burst_size;    /* 0x0c */
} GenetDmaCtrl;
_Static_assert(offsetof(GenetDmaCtrl, scb_burst_size) == 0x0c, "layout");

#define DMA_EN (1u << 0)
#define DMA_DISABLED (1u << 0)

#define MII_BMSR 1
#define MII_PHYSID1 2
#define MII_PHYSID2 3
#define MII_ADVERTISE 4
#define MII_LPA 5
#define MII_CTRL1000 9
#define MII_STAT1000 10

#define BMSR_LSTATUS (1u << 2)
#define BMSR_ANEGCOMPLETE (1u << 5)
#define ADV_10HALF (1u << 5)
#define ADV_10FULL (1u << 6)
#define ADV_100HALF (1u << 7)
#define ADV_100FULL (1u << 8)
#define CTRL1000_FULL (1u << 9)
#define STAT1000_FULL (1u << 11)

typedef union {
    uint32_t raw;
    struct {
        uint32_t data : 16;   /* 15:0  */
        uint32_t reg  : 5;    /* 20:16 */
        uint32_t phy  : 5;    /* 25:21 */
        uint32_t op   : 2;    /* 27:26 */
        uint32_t fail : 1;    /* 28    */
        uint32_t busy : 1;    /* 29    */
        uint32_t      : 2;    /* 31:30 */
    };
} GenetMdioCmd;
_Static_assert(sizeof(GenetMdioCmd) == 4, "MDIO_CMD is one word");

#endif /* USER_DRIVERS_GENETV5DRV_GENETV5DRV_H */