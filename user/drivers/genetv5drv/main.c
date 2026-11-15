#include "dev/devm.h"
#include "genetv5.h"
#include <dev/protocols/mbox.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/service.h>
#include <zuzu/syspage.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "genetv5drv"

#define UDBG_ENABLED 1

#include <stdio.h>
#include <zuzu/udbg.h>

#define PHY_ADDR 1
#define MDIO_TIMEOUT 100000
#define DMA_STOP_TIMEOUT_MS 10
#define ANEG_POLL_MS 100
#define ANEG_TIMEOUT_MS 15000
#define ADVERTISE_1000 0

static const char *compats[1] = {"brcm,bcm2711-genet-v5"};

static Handle g_genet_h = -1;
static uint8_t *genet_base = NULL;
static GenetSys *genet_sys = NULL;
static GenetExt *genet_ext = NULL;
static GenetRbuf *genet_rbuf = NULL;
static GenetUmac *genet_umac = NULL;
static GenetDmaCtrl *genet_rdma = NULL;
static GenetDmaCtrl *genet_tdma = NULL;
static GenetTxRing *genet_tx_ring = NULL;
static GenetRxRing *genet_rx_ring = NULL;

static uint8_t mac[6];
static Handle g_tx_buf_h = -1;
static Handle g_rx_buf_h = -1;

static Handle WaitForService(const char *path)
{
    for (;;) {
        Handle h = LookupService(path);
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

static Err MdioRead(uint32_t reg, uint16_t *out)
{
    GenetMdioCmd c = {.raw = 0};
    c.op = 2;
    c.phy = PHY_ADDR;
    c.reg = reg;
    c.busy = 1;
    genet_umac->mdio_cmd = c.raw; /* one write, everything at once */

    uint32_t timeout = MDIO_TIMEOUT;
    do {
        c.raw = genet_umac->mdio_cmd;
    } while (c.busy && --timeout); /* one read per loop */
    if (!timeout)
        return ERR_TIMEOUT;
    if (c.fail)
        return ERR_IO;
    *out = c.data;
    return ZUZU_OK;
}

static Err MdioWrite(uint32_t reg, uint16_t value)
{
    GenetMdioCmd c = {.raw = 0};
    c.op = 1;
    c.phy = PHY_ADDR;
    c.reg = reg;
    c.data = value;
    c.busy = 1;
    genet_umac->mdio_cmd = c.raw;

    uint32_t timeout = MDIO_TIMEOUT;
    do {
        c.raw = genet_umac->mdio_cmd;
    } while (c.busy && --timeout);
    if (!timeout)
        return ERR_TIMEOUT;
    return ZUZU_OK;
}

static Err MapRegs(void)
{
    g_genet_h = RequestDevice(WaitForService("/svc/devsvc"), compats, 1, NULL);
    if (g_genet_h < 0) {
        LOG_ERROR(LOG_TAG, "RequestDevice failed: %s", StrToError(g_genet_h));
        return g_genet_h;
    }

    void *va = MemMap(g_genet_h, 0, PROT_RW);
    if (PtrIsErr(va)) {
        LOG_ERROR(LOG_TAG, "MemMap failed: %s", StrToError((Err)(VirtAddr)va));
        return (Err)(VirtAddr)va;
    }
    genet_base = va;
    genet_sys = (GenetSys *)(genet_base + GENET_SYS_OFF);
    genet_ext = (GenetExt *)(genet_base + GENET_EXT_OFF);
    genet_rbuf = (GenetRbuf *)(genet_base + GENET_RBUF_OFF);
    genet_umac = (GenetUmac *)(genet_base + GENET_UMAC_OFF);
    genet_rdma = (GenetDmaCtrl *)(genet_base + GENET_RDMA_CTRL_OFF);
    genet_tdma = (GenetDmaCtrl *)(genet_base + GENET_TDMA_CTRL_OFF);
    return ZUZU_OK;
}

static Err CheckRev(void)
{
    uint32_t rev = genet_sys->rev_ctrl;
    uint32_t major = (rev >> 24) & 0xf;
    if (major != 6) {
        LOG_ERROR(LOG_TAG, "not GENET v5: rev_ctrl 0x%08x", rev);
        return ERR_BADTYPE;
    }
    LOG_INFO(LOG_TAG, "GENET v5.%u", (rev >> 16) & 0xf);
    return ZUZU_OK;
}

static Err GetMac(void)
{
    Err rc = MboxGetMac(WaitForService(MBOX_SERVICE), mac);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "MboxGetMac failed: %s", StrToError(rc));
        return rc;
    }
    LOG_INFO(LOG_TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return ZUZU_OK;
}

static Err DmaStop(void)
{
    genet_tdma->ctrl &= ~DMA_EN;
    genet_rdma->ctrl &= ~DMA_EN;

    for (uint32_t ms = 0; ms < DMA_STOP_TIMEOUT_MS; ms++) {
        if ((genet_tdma->status & DMA_DISABLED) && (genet_rdma->status & DMA_DISABLED))
            break;
        Sleep(1);
    }
    if (!(genet_tdma->status & DMA_DISABLED) || !(genet_rdma->status & DMA_DISABLED)) {
        LOG_ERROR(LOG_TAG, "DMA did not stop: tdma 0x%08x rdma 0x%08x", genet_tdma->status,
                  genet_rdma->status);
        return ERR_TIMEOUT;
    }

    genet_umac->tx_flush = 1;
    Sleep(1);
    genet_umac->tx_flush = 0;
    return ZUZU_OK;
}

static void ExtPowerUp(void)
{
    uint32_t reg = genet_ext->pwr_mgmt & ~(EXT_PWR_DOWN_BIAS | EXT_PWR_DOWN_DLL | EXT_ENERGY_DET_MASK);
    reg &= ~(EXT_PWR_DOWN_PHY_EN | EXT_PWR_DOWN_PHY_RD | EXT_PWR_DOWN_PHY_SD |
             EXT_PWR_DOWN_PHY_RX | EXT_PWR_DOWN_PHY_TX | EXT_IDDQ_GLBL_PWR);
    reg |= EXT_PHY_RESET;
    genet_ext->pwr_mgmt = reg;
    Sleep(1);
    reg &= ~EXT_PHY_RESET;
    genet_ext->pwr_mgmt = reg;
}

static void UmacReset(void)
{
    genet_sys->port_ctrl = PORT_MODE_EXT_GPHY;

    genet_sys->rbuf_flush_ctrl |= RBUF_FLUSH_RESET;
    Sleep(1);
    genet_sys->rbuf_flush_ctrl &= ~RBUF_FLUSH_RESET;
    Sleep(1);
    genet_sys->rbuf_flush_ctrl = 0;
    Sleep(1);

    genet_umac->cmd = 0;
    genet_umac->cmd = CMD_SW_RESET | CMD_LCL_LOOP_EN;
    Sleep(1);
    genet_umac->cmd = 0;

    genet_umac->mib_ctrl = MIB_RESET_ALL;
    genet_umac->mib_ctrl = 0;

    genet_umac->max_frame_len = ENET_MAX_FRAME_LEN;

    genet_rbuf->ctrl |= RBUF_ALIGN_2B;
    genet_rbuf->tbuf_size_ctrl = 1;
}

static void WriteMac(void)
{
    genet_umac->mac0 =
        ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) | ((uint32_t)mac[2] << 8) | mac[3];
    genet_umac->mac1 = ((uint32_t)mac[4] << 8) | mac[5];
}

static Err PhyNegotiate(void)
{
    uint16_t ctrl1000 = 0, bmsr = 0;
    Err rc = MdioRead(MII_CTRL1000, &ctrl1000);
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_ADVERTISE,
                       ADV_CSMA | ADV_10HALF | ADV_10FULL | ADV_100HALF | ADV_100FULL);
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_CTRL1000,
                       (uint16_t)((ctrl1000 & ~(CTRL1000_HALF | CTRL1000_FULL)) |
                                  (ADVERTISE_1000 ? CTRL1000_FULL : 0)));
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "starting autonegotiation failed: %s", StrToError(rc));
        return rc;
    }

    for (uint32_t ms = 0; ms < ANEG_TIMEOUT_MS; ms += ANEG_POLL_MS) {
        Sleep(ANEG_POLL_MS);
        rc = MdioRead(MII_BMSR, &bmsr);
        if (rc == ZUZU_OK)
            rc = MdioRead(MII_BMSR, &bmsr);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "MDIO read failed: %s", StrToError(rc));
            return rc;
        }
        if ((bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE)) {
            LOG_INFO(LOG_TAG, "autonegotiation done after %u ms", ms + ANEG_POLL_MS);
            return ZUZU_OK;
        }
    }
    LOG_ERROR(LOG_TAG, "autonegotiation timed out, bmsr %04x", bmsr);
    return ERR_TIMEOUT;
}

static Err PhyGetSpeed(uint32_t *umac_speed, uint32_t *mbps)
{
    uint16_t id1 = 0, id2 = 0, bmsr = 0, adv = 0, lpa = 0, ctrl1000 = 0, stat1000 = 0;
    Err rc = MdioRead(MII_PHYSID1, &id1);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_PHYSID2, &id2);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_BMSR, &bmsr);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_BMSR, &bmsr);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_ADVERTISE, &adv);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_LPA, &lpa);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_CTRL1000, &ctrl1000);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_STAT1000, &stat1000);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "MDIO read failed: %s", StrToError(rc));
        return rc;
    }
    LOG_INFO(LOG_TAG, "PHY id %04x:%04x link %u aneg %u", id1, id2, !!(bmsr & BMSR_LSTATUS),
             !!(bmsr & BMSR_ANEGCOMPLETE));

    if (!(bmsr & BMSR_LSTATUS) || !(bmsr & BMSR_ANEGCOMPLETE)) {
        LOG_ERROR(LOG_TAG, "no link");
        return ERR_NOTCONN;
    }

    LOG_INFO(LOG_TAG, "adv %04x lpa %04x ctrl1000 %04x stat1000 %04x", adv, lpa, ctrl1000,
             stat1000);
    uint16_t common = adv & lpa;
    if ((ctrl1000 & CTRL1000_FULL) && (stat1000 & STAT1000_FULL)) {
        *umac_speed = UMAC_SPEED_1000;
        *mbps = 1000;
    } else if (common & (ADV_100FULL | ADV_100HALF)) {
        *umac_speed = UMAC_SPEED_100;
        *mbps = 100;
    } else {
        *umac_speed = UMAC_SPEED_10;
        *mbps = 10;
    }
    return ZUZU_OK;
}

static void SetLink(uint32_t umac_speed)
{
    uint32_t oob = genet_ext->rgmii_oob_ctrl;
    oob &= ~(OOB_DISABLE | ID_MODE_DIS);
    oob |= RGMII_LINK | RGMII_MODE_EN;
    genet_ext->rgmii_oob_ctrl = oob;

    genet_umac->cmd = umac_speed << CMD_SPEED_SHIFT;
}

int main()
{
    Err rc = MapRegs();
    if (rc == ZUZU_OK)
        rc = CheckRev();
    if (rc == ZUZU_OK)
        rc = GetMac();
    if (rc == ZUZU_OK)
        rc = DmaStop();
    if (rc != ZUZU_OK)
        return rc;

    ExtPowerUp();
    UmacReset();
    WriteMac();

    uint32_t umac_speed = 0, mbps = 0;
    rc = PhyNegotiate();
    if (rc == ZUZU_OK)
        rc = PhyGetSpeed(&umac_speed, &mbps);
    if (rc != ZUZU_OK)
        return rc;
    SetLink(umac_speed);

    LOG_INFO(LOG_TAG, "link %u Mb/s; cmd 0x%08x oob 0x%08x mac0 0x%08x mac1 0x%08x", mbps,
             genet_umac->cmd, genet_ext->rgmii_oob_ctrl, genet_umac->mac0, genet_umac->mac1);

    g_tx_buf_h = CreateMem(1, MEM_CONTIG | MEM_UNCACHED);
    if (g_tx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_tx_buf_h));
        return g_tx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet_h, g_tx_buf_h, 0, 4096, DMA_TO_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
    uint8_t *tx_buf = MemMap(g_tx_buf_h, 0, PROT_RW);
    if (PtrIsErr(tx_buf)) {
        LOG_ERROR(LOG_TAG, "MemMap of tx buffer failed: %s", StrToError((Err)(VirtAddr)tx_buf));
        return (Err)(VirtAddr)tx_buf;
    }

    genet_tx_ring = (GenetTxRing *)(genet_base + 0x5000);
    GenetDesc *tx_desc = (GenetDesc *)(genet_base + 0x4000);

    genet_tdma->scb_burst_size = 8;
    genet_tx_ring->start_addr = 0;
    genet_tx_ring->read_ptr = 0;
    genet_tx_ring->write_ptr = 0;
    genet_tx_ring->end_addr = (256 * 3) - 1;
    genet_tx_ring->prod_index = genet_tx_ring->cons_index;
    genet_tx_ring->mbuf_done_thresh = 1;
    genet_tx_ring->flow_period = 0;
    genet_tx_ring->ring_buf_size = (256 << 16) | 2048;
    genet_tdma->ring_cfg = 1 << 16;

    genet_tdma->ctrl  = (1 << 17) | 1;
    genet_umac->cmd  |= CMD_TX_EN;

    static const char payload[] = "hello from zuzu";
    memset(tx_buf, 0, 60);
    memset(tx_buf, 0xff, 6);
    memcpy(tx_buf + 6, mac, 6);
    tx_buf[12] = 0x88;
    tx_buf[13] = 0xb5;
    memcpy(tx_buf + 14, payload, sizeof(payload));

    uint32_t prod = genet_tx_ring->prod_index & 0xffff;
    uint32_t n = prod & 0xff;
    tx_desc[n].addr_lo = (uint32_t)res.bus_addr;
    tx_desc[n].addr_hi = 0;
    tx_desc[n].length_status = (60u << 16) | (0x3fu << 7) | 0x4000u | 0x2000u | 0x0040u;
    genet_tx_ring->prod_index = prod + 1;

    Sleep(10);
    LOG_INFO(LOG_TAG, "tx: prod %u cons %u", prod + 1, genet_tx_ring->cons_index & 0xffff);

    genet_rx_ring = (GenetRxRing *)(genet_base + 0x3000);
    GenetDesc *rx_desc = (GenetDesc *)(genet_base + 0x2000);

    g_rx_buf_h = CreateMem(128, MEM_CONTIG | MEM_UNCACHED);
    if (g_rx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_rx_buf_h));
        return g_rx_buf_h;
    }
    
    DmaMapResult rx_res = DmaMap(g_genet_h, g_rx_buf_h, 0, 4096 * 128, DMA_FROM_DEVICE);
    if (rx_res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(rx_res.status));
        return rx_res.status;
    }
    uint8_t *rx_buf = MemMap(g_rx_buf_h, 0, PROT_RW);
    if (PtrIsErr(rx_buf)) {
        LOG_ERROR(LOG_TAG, "MemMap of rx buffer failed: %s", StrToError((Err)(VirtAddr)rx_buf));
        return (Err)(VirtAddr)rx_buf;
    }

    genet_rdma->scb_burst_size = 8;
    genet_rx_ring->start_addr = 0;
    genet_rx_ring->read_ptr = 0;
    genet_rx_ring->write_ptr = 0;
    genet_rx_ring->end_addr = (256 * 3) - 1;
    genet_rx_ring->cons_index = genet_rx_ring->prod_index;
    genet_rx_ring->mbuf_done_thresh = 1;
    genet_rx_ring->xon_xoff_thresh = 	(5 << 16) | (256 >> 4);
    genet_rx_ring->ring_buf_size = (256 << 16) | 2048;
    genet_rdma->ring_cfg = 1 << 16;

    for (int i = 0; i < 256; i++) {
        rx_desc[i].addr_lo       = rx_res.bus_addr + (i * 2048);
        rx_desc[i].addr_hi       = 0;
        rx_desc[i].length_status = (2048 << 16) | 0x8000;
    }

    genet_rdma->ctrl  = (1 << 17) | 1;
    genet_umac->cmd  |= CMD_RX_EN;
    
    uint32_t cons = genet_rx_ring->cons_index & 0xffff;
    for (;;) {
        uint32_t prod = genet_rx_ring->prod_index & 0xffff;
        while (cons != prod) {
            uint32_t i = cons & 0xff;
            uint32_t ls = rx_desc[i].length_status;
            uint32_t len = (ls >> 16) & 0xfff;
            uint8_t *f = rx_buf + (i * 2048) + 2;          /* 2 pad bytes from RBUF_ALIGN_2B */
            LOG_INFO(LOG_TAG, "rx %u bytes from %02x:%02x:%02x:%02x:%02x:%02x type %02x%02x flags %04x",
                     len - 2, f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], ls & 0xffff);
            cons = (cons + 1) & 0xffff;
            genet_rx_ring->cons_index = cons;            /* hand the buffer back */
        }
        Sleep(10);
    }
}
