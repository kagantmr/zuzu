#ifndef USER_DRIVERS_GENETV5DRV_GENET_H
#define USER_DRIVERS_GENETV5DRV_GENET_H

#include "genetv5.h"
#include <zuzu/zuzu.h>

#define LOG_TAG "genetv5drv"

#define GENET_RING_SIZE 256
#define GENET_BUF_SIZE 2048
#define GENET_BUF_PAGES (GENET_RING_SIZE * GENET_BUF_SIZE / 4096)
#define GENET_MIN_FRAME 60
/** @brief Size of one DMA descriptor in 32-bit words; ring pointers count in these. */
#define GENET_DESC_WORDS 3
#define GENET_LINK_POLL_MS 250
/** @brief Queued TX frames with no completion for this long mean the ring is stuck. */
#define GENET_TX_TIMEOUT_MS 2000
#define GENET_IRQ_BIT 2
#define GENET_IRQ_MASK (1u << GENET_IRQ_BIT)
/** @brief Most RX frames GenetRxPoll() handles per call. */
#define GENET_RX_BUDGET 64

/** @brief Order CPU writes to DMA memory before a later doorbell write. */
static inline void GenetWmb(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
/** @brief Order CPU reads of DMA memory before a later doorbell write. */
static inline void GenetRmb(void) { __asm__ volatile("dmb osh" ::: "memory"); }

/** @brief Called by GenetRxPoll() once per good frame; @p frame is valid until it returns. */
typedef void (*GenetRxFn)(const uint8_t *frame, uint32_t len);

/** @brief Driver state shared by all genetv5drv files. */
typedef struct {
    Handle dev, event;
    uint8_t *base;
    GenetSys *sys;
    GenetExt *ext;
    GenetRbuf *rbuf;
    GenetUmac *umac;
    GenetDmaCtrl *rdma;
    GenetDmaCtrl *tdma;
    GenetTxRing *tx_ring;
    GenetRxRing *rx_ring;
    GenetDesc *tx_desc;
    GenetDesc *rx_desc;
    GenetIntrl2 *intrl2_0;
    uint8_t mac[6];
    bool link_up;
    uint32_t mbps;
    uint32_t tx_packets, tx_busy;
    uint32_t rx_packets, rx_errors;
    uint32_t tx_completed, tx_timeouts;
    uint32_t tx_underruns, rx_overflows, irq_other;
    uint32_t irqs;
} Genet;

/** @brief The one GENET instance. */
extern Genet g_genet;

/** @brief Read a PHY register over MDIO. */
Err MdioRead(uint32_t reg, uint16_t *out);
/** @brief Write a PHY register over MDIO. */
Err MdioWrite(uint32_t reg, uint16_t value);
/** @brief Read a PHY register twice so latched status bits show the current state. */
Err MdioReadLatched(uint32_t reg, uint16_t *out);

/** @brief Restart autonegotiation. */
Err PhyStartAutoNeg(void);
/** @brief Read the negotiated speed from the PHY. */
Err PhyGetSpeed(uint32_t *umac_speed, uint32_t *mbps);
/** @brief Check the PHY link and reprogram the MAC when it changes. */
void PhyPollLink(void);

/** @brief Request the device and map its registers. */
Err MapRegs(void);
/** @brief Check that the block is GENET v5. */
Err CheckRev(void);
/** @brief Fetch the board MAC address from the mailbox. */
Err GetMac(void);
/** @brief Stop both DMA engines and flush TX. */
Err DmaStop(void);
/** @brief Remove DMA mappings attached to the device. */
Err DmaReclaim(void);
/** @brief Power up the EXT block and reset the PHY. */
void ExtPowerUp(void);
/** @brief Reset the UMAC, RBUF and MIB counters. */
void UmacReset(void);
/** @brief Program the MAC address into the UMAC. */
void WriteMac(void);
/** @brief Configure RGMII and the UMAC speed, keeping the TX/RX enables. */
void SetLink(uint32_t umac_speed);

/** @brief Mask every GENET interrupt, create the shared event and bind the IRQ to GENET_IRQ_BIT. */
Err GenetIrqInit(void);
/** @brief Let the given INTRL2_0 sources raise the IRQ. */
void GenetIrqEnable(uint32_t bits);
/** @brief Stop the given INTRL2_0 sources from raising the IRQ. */
void GenetIrqDisable(uint32_t bits);
/** @brief Acknowledge the IRQ, rearm it and return the unmasked sources that fired. */
uint32_t GenetIrqAck(void);

/** @brief Set up the TX buffers and ring 16, and enable TX. */
Err GenetTxInit(void);
/** @brief Program and enable TX ring 16, starting from the index the hardware already holds. */
void GenetTxRingStart(void);
/** @brief Queue one frame on ring 16; ERR_BUSY when the ring is full. */
Err GenetTx(const void *frame, uint32_t len);
/** @brief Count TX frames the hardware finished since the last call. */
void GenetTxReclaim(void);
/** @brief True, and counted, when queued TX frames stalled for GENET_TX_TIMEOUT_MS. */
bool GenetTxStuck(uint64_t now);

/** @brief Set up the RX buffers and ring 16, and enable RX. */
Err GenetRxInit(void);
/** @brief Re-arm every RX descriptor and enable ring 16 from the hardware's current index. */
void GenetRxRingStart(void);
/** @brief Pass up to GENET_RX_BUDGET good frames to @p fn and recycle their buffers; true if frames remain. */
bool GenetRxPoll(GenetRxFn fn)

#endif /* USER_DRIVERS_GENETV5DRV_GENET_H */
