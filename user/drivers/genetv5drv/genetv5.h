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
#define GENET_INTRL2_0_OFF 0x0200

#define PORT_MODE_EXT_GPHY 3
#define RBUF_FLUSH_RESET (1u << 1)

typedef struct {
    volatile uint32_t pwr_mgmt;          /* 0x00 */
    uint32_t _rsvd0[2];
    volatile uint32_t rgmii_oob_ctrl;    /* 0x0c */
} GenetExt;
_Static_assert(offsetof(GenetExt, rgmii_oob_ctrl) == 0x0c, "layout");

#define EXT_PWR_DOWN_BIAS (1u << 0)
#define EXT_PWR_DOWN_DLL (1u << 1)
#define EXT_ENERGY_DET_MASK (1u << 12)
#define EXT_IDDQ_GLBL_PWR (1u << 7)
#define EXT_PHY_RESET (1u << 8)
#define EXT_PWR_DOWN_PHY_TX (1u << 16)
#define EXT_PWR_DOWN_PHY_RX (1u << 17)
#define EXT_PWR_DOWN_PHY_SD (1u << 18)
#define EXT_PWR_DOWN_PHY_RD (1u << 19)
#define EXT_PWR_DOWN_PHY_EN (1u << 20)

#define CMD_PROMISC (1u << 4)
/** @brief Number of destination MAC filter slots in the UniMAC. */
#define MDF_SLOTS 17

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
    uint32_t _rsvd3[(0x650 - 0x618) / 4];
    volatile uint32_t mdf_ctrl;     /* 0x650 */
    volatile uint32_t mdf_addr[34]; /* 0x654 */
} GenetUmac;
_Static_assert(offsetof(GenetUmac, tx_flush) == 0x334, "layout");
_Static_assert(offsetof(GenetUmac, mib_ctrl) == 0x580, "layout");
_Static_assert(offsetof(GenetUmac, mdio_cmd) == 0x614, "layout");

#define CMD_TX_EN (1u << 0)
#define CMD_RX_EN (1u << 1)
#define CMD_SPEED_SHIFT 2
#define CMD_SW_RESET (1u << 13)
#define CMD_LCL_LOOP_EN (1u << 15)
#define CMD_RX_PAUSE_IGNORE (1u << 8)
#define CMD_TX_PAUSE_IGNORE (1u << 28)

#define UMAC_SPEED_10 0
#define UMAC_SPEED_100 1
#define UMAC_SPEED_1000 2

#define MIB_RESET_ALL 0x7u
/** @brief UMAC offset of the MIB counter for bytes received. */
#define MIB_RX_BYTES 0x42c
/** @brief UMAC offset of the MIB counter for received frames with a bad FCS. */
#define MIB_RX_FCS 0x438
/** @brief UMAC offset of the MIB counter for received frames with an alignment error. */
#define MIB_RX_ALIGN 0x448
/** @brief UMAC offset of the MIB counter for received oversize frames. */
#define MIB_RX_OVERSIZE 0x458
/** @brief UMAC offset of the MIB counter for bytes transmitted. */
#define MIB_TX_BYTES 0x4e8
/** @brief UMAC offset of the MIB counter for received runt frames. */
#define MIB_RX_RUNT 0x500
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

typedef struct {
    volatile uint32_t read_ptr;
    volatile uint32_t read_ptr_hi;
    volatile uint32_t cons_index;
    volatile uint32_t prod_index;
    volatile uint32_t ring_buf_size;
    volatile uint32_t start_addr;
    volatile uint32_t start_addr_hi;
    volatile uint32_t end_addr;
    volatile uint32_t end_addr_hi;
    volatile uint32_t mbuf_done_thresh;
    volatile uint32_t flow_period;
    volatile uint32_t write_ptr;
} GenetTxRing;
_Static_assert(offsetof(GenetTxRing, prod_index) == 0x0c, "layout");
_Static_assert(offsetof(GenetTxRing, end_addr) == 0x1c, "layout");
_Static_assert(offsetof(GenetTxRing, write_ptr) == 0x2c, "layout");

typedef struct {
    volatile uint32_t write_ptr;
    volatile uint32_t write_ptr_hi;
    volatile uint32_t prod_index;
    volatile uint32_t cons_index;
    volatile uint32_t ring_buf_size;
    volatile uint32_t start_addr;
    volatile uint32_t start_addr_hi;
    volatile uint32_t end_addr;
    volatile uint32_t end_addr_hi;
    volatile uint32_t mbuf_done_thresh;
    volatile uint32_t xon_xoff_thresh;
    volatile uint32_t read_ptr;
} GenetRxRing;
_Static_assert(offsetof(GenetRxRing, xon_xoff_thresh) == 0x28, "layout");
_Static_assert(offsetof(GenetRxRing, read_ptr) == 0x2c, "layout");

typedef struct {
    volatile uint32_t length_status;   /* 0x0: length in 27:16, flags in 15:0 */
    volatile uint32_t addr_lo;         /* 0x4: bus address of the frame */
    volatile uint32_t addr_hi;         /* 0x8: 0 for you */
} GenetDesc;
_Static_assert(sizeof(GenetDesc) == 12, "descriptor is 3 words");

#define DESC_LEN_SHIFT 16
#define DESC_LEN_MASK 0xfffu
#define DESC_OWN (1u << 15)
#define DESC_EOP (1u << 14)
#define DESC_SOP (1u << 13)
#define DESC_TX_QTAG (0x3fu << 7)
#define DESC_TX_APPEND_CRC (1u << 6)
#define DESC_RX_BRDCAST (1u << 6)
#define DESC_RX_MULT (1u << 5)
#define DESC_RX_LG (1u << 4)
#define DESC_RX_NO (1u << 3)
#define DESC_RX_RXER (1u << 2)
#define DESC_RX_CRC_ERR (1u << 1)
#define DESC_RX_OV (1u << 0)
#define DESC_RX_ERRORS 0x1fu

#define GENET_RDESC_OFF 0x2000
#define GENET_RRING16_OFF 0x3000
#define GENET_TDESC_OFF 0x4000
#define GENET_TRING16_OFF 0x5000
#define DMA_RING16_CFG_EN (1u << 16)
#define DMA_RING16_BUF_EN (1u << 17)

typedef struct {
    volatile uint32_t stat;
    volatile uint32_t stat_set;
    volatile uint32_t clear;
    volatile uint32_t mask_status;
    volatile uint32_t mask_set;
    volatile uint32_t mask_clear;
} GenetIntrl2;
_Static_assert(offsetof(GenetIntrl2, clear) == 0x08, "layout");
_Static_assert(offsetof(GenetIntrl2, mask_clear) == 0x14, "layout");

/** @brief INTRL2_0: the TX buffer ran dry in the middle of a frame. */
#define TBUF_UNDERRUN (1u << 8)
/** @brief INTRL2_0: the RX buffer overflowed and dropped frames. */
#define RBUF_OVERFLOW (1u << 9)
#define RXDMA_MBDONE (1u << 13)

#define TXDMA_MBDONE (1u << 16)

#endif /* USER_DRIVERS_GENETV5DRV_GENETV5DRV_H */