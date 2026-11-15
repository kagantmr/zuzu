#ifndef USER_DRIVERS_GENETV5DRV_GENET_H
#define USER_DRIVERS_GENETV5DRV_GENET_H

#include "genetv5.h"
#include <zuzu/zuzu.h>

#define LOG_TAG "genetv5drv"

#define GENET_RING_SIZE 256
#define GENET_BUF_SIZE 2048

/** @brief Driver state shared by all genetv5drv files. */
typedef struct {
    Handle dev;
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
    uint8_t mac[6];
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
/** @brief Configure RGMII and the UMAC speed. */
void SetLink(uint32_t umac_speed);

/** @brief Set up the TX buffer and ring 16, and enable TX. */
Err GenetTxInit(void);
/** @brief Queue one frame on ring 16. */
void GenetTx(const void *frame, uint32_t len);

/** @brief Set up the RX buffers and ring 16, and enable RX. */
Err GenetRxInit(void);
/** @brief Log every frame received since the last call. */
void GenetRxPoll(void);

#endif /* USER_DRIVERS_GENETV5DRV_GENET_H */
