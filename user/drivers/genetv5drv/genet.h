#ifndef USER_DRIVERS_GENETV5DRV_GENET_H
#define USER_DRIVERS_GENETV5DRV_GENET_H

#include "genetv5.h"
#include <zuzu/zuzu.h>

#define LOG_TAG "genetv5drv"

#define GENET_RING_SIZE 256
#define GENET_BUF_SIZE 2048
#define GENET_BUF_PAGES (GENET_RING_SIZE * GENET_BUF_SIZE / 4096)
#define GENET_MIN_FRAME 60
#define GENET_LINK_POLL_MS 1000

/** @brief Order CPU writes to DMA memory before a later doorbell write. */
static inline void GenetWmb(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
/** @brief Order CPU reads of DMA memory before a later doorbell write. */
static inline void GenetRmb(void) { __asm__ volatile("dmb osh" ::: "memory"); }

/** @brief Called by GenetRxPoll() once per good frame; @p frame is valid until it returns. */
typedef void (*GenetRxFn)(const uint8_t *frame, uint32_t len);

/** @brief Driver state shared by all genetv5drv files. */
typedef struct {
    Handle dev, irq;
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
} Genet;

/** @brief The one GENET instance. */
extern Genet g_genet;

/** @brief Read a PHY register over MDIO. */
Err MdioRead(uint32_t reg, uint16_t *out);
/** @brief Write a PHY register over MDIO. */
Err MdioWrite(uint32_t reg, uint16_t value);

/** @brief Restart autonegotiation and wait for link. */
Err PhyNegotiate(void);
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
/** @brief Power up the EXT block and reset the PHY. */
void ExtPowerUp(void);
/** @brief Reset the UMAC, RBUF and MIB counters. */
void UmacReset(void);
/** @brief Program the MAC address into the UMAC. */
void WriteMac(void);
/** @brief Configure RGMII and the UMAC speed, keeping the TX/RX enables. */
void SetLink(uint32_t umac_speed);

/** @brief Mask and clear every GENET interrupt, then bind the IRQ to an event. */
Err GenetIrqInit(void);
/** @brief Let the given INTRL2_0 sources raise the IRQ. */
void GenetIrqEnable(uint32_t bits);
/** @brief Wait up to @p timeout ms for the IRQ, acknowledge it and return the sources that fired.
 */
uint32_t GenetIrqWait(Duration timeout);

/** @brief Set up the TX buffers and ring 16, and enable TX. */
Err GenetTxInit(void);
/** @brief Queue one frame on ring 16; ERR_BUSY when the ring is full. */
Err GenetTx(const void *frame, uint32_t len);

/** @brief Set up the RX buffers and ring 16, and enable RX. */
Err GenetRxInit(void);
/** @brief Pass every good frame received since the last call to @p fn and recycle its buffer. */
void GenetRxPoll(GenetRxFn fn);

#endif /* USER_DRIVERS_GENETV5DRV_GENET_H */
