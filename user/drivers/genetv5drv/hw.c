#include "dev/devm.h"
#include "genet.h"
#include "netd.h"
#include <dev/protocols/mbox.h>
#include <util/log.h>
#include <zuzu/service.h>

#define DMA_STOP_TIMEOUT_MS 10

Genet g_genet = {.dev = -1, .event = -1};

static const char *compats[1] = {"brcm,bcm2711-genet-v5"};

Err MapRegs(void)
{
    g_genet.dev = RequestDevice(WaitForService("/svc/devsvc"), compats, 1, NULL);
    if (g_genet.dev < 0) {
        LOG_ERROR(LOG_TAG, "RequestDevice failed: %s", StrToError(g_genet.dev));
        return g_genet.dev;
    }

    void *va = MemMap(g_genet.dev, 0, PROT_RW);
    if (PtrIsErr(va)) {
        LOG_ERROR(LOG_TAG, "MemMap failed: %s", StrToError((Err)(VirtAddr)va));
        return (Err)(VirtAddr)va;
    }
    g_genet.base = va;
    g_genet.sys = (GenetSys *)(g_genet.base + GENET_SYS_OFF);
    g_genet.ext = (GenetExt *)(g_genet.base + GENET_EXT_OFF);
    g_genet.rbuf = (GenetRbuf *)(g_genet.base + GENET_RBUF_OFF);
    g_genet.umac = (GenetUmac *)(g_genet.base + GENET_UMAC_OFF);
    g_genet.rdma = (GenetDmaCtrl *)(g_genet.base + GENET_RDMA_CTRL_OFF);
    g_genet.tdma = (GenetDmaCtrl *)(g_genet.base + GENET_TDMA_CTRL_OFF);
    g_genet.intrl2_0 = (GenetIntrl2 *)(g_genet.base + GENET_INTRL2_0_OFF);

    return ZUZU_OK;
}

Err CheckRev(void)
{
    uint32_t rev = g_genet.sys->rev_ctrl;
    uint32_t major = (rev >> 24) & 0xf;
    if (major != GENET_REV_MAJOR_V5) {
        LOG_ERROR(LOG_TAG, "not GENET v5: rev_ctrl 0x%08x", rev);
        return ERR_BADTYPE;
    }
    LOG_INFO(LOG_TAG, "GENET v5.%u", (rev >> 16) & 0xf);
    return ZUZU_OK;
}

Err GetMac(void)
{
    Err rc = MboxGetMac(WaitForService(MBOX_SERVICE), g_genet.mac);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "MboxGetMac failed: %s", StrToError(rc));
        return rc;
    }
    LOG_INFO(LOG_TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x", g_genet.mac[0], g_genet.mac[1],
             g_genet.mac[2], g_genet.mac[3], g_genet.mac[4], g_genet.mac[5]);
    return ZUZU_OK;
}

Err DmaStop(void)
{
    g_genet.tdma->ctrl &= ~DMA_EN;
    g_genet.rdma->ctrl &= ~DMA_EN;

    for (uint32_t ms = 0; ms < DMA_STOP_TIMEOUT_MS; ms++) {
        if ((g_genet.tdma->status & DMA_DISABLED) && (g_genet.rdma->status & DMA_DISABLED))
            break;
        Sleep(1);
    }
    if (!(g_genet.tdma->status & DMA_DISABLED) || !(g_genet.rdma->status & DMA_DISABLED)) {
        LOG_ERROR(LOG_TAG, "DMA did not stop: tdma 0x%08x rdma 0x%08x", g_genet.tdma->status,
                  g_genet.rdma->status);
        return ERR_TIMEOUT;
    }

    g_genet.umac->tx_flush = 1;
    Sleep(1);
    g_genet.umac->tx_flush = 0;
    return ZUZU_OK;
}

Err DmaReclaim(void)
{
    SvcResult r;
    size_t count = 0;
    Err rc;
    for (;;) {
        r = HandleQueryAt(g_genet.dev, QUERY_DMA_ID, 0);
        if (r.r0 != ZUZU_OK)
            break;
        if ((rc = DmaUnmap(g_genet.dev, r.r1)) != ZUZU_OK) {
            return (rc);
        }
        count++;
    }
    if (count > 0)
        LOG_INFO(LOG_TAG, "Reclaimed %zu mappings", count);

    return ZUZU_OK;
}

void GenetShutdown(void)
{
    if (g_genet.intrl2_0)
        g_genet.intrl2_0->mask_set = GENET_IRQ_ALL;
    if (g_genet.tdma && g_genet.rdma)
        DmaStop();
    NetdDisconnect();
    if (g_genet.dev >= 0)
        DmaReclaim();
    GenetTxFree();
    GenetRxFree();
    if (g_genet.event >= 0)
        HandleClose(g_genet.event);
    if (g_genet.base)
        MemUnmap(g_genet.base);
    if (g_genet.dev >= 0)
        HandleClose(g_genet.dev);
    g_genet = (Genet){.dev = -1, .event = -1};
}

void ExtPowerUp(void)
{
    uint32_t reg =
        g_genet.ext->pwr_mgmt & ~(EXT_PWR_DOWN_BIAS | EXT_PWR_DOWN_DLL | EXT_ENERGY_DET_MASK);
    reg &= ~(EXT_PWR_DOWN_PHY_EN | EXT_PWR_DOWN_PHY_RD | EXT_PWR_DOWN_PHY_SD | EXT_PWR_DOWN_PHY_RX |
             EXT_PWR_DOWN_PHY_TX | EXT_IDDQ_GLBL_PWR);
    reg |= EXT_PHY_RESET;
    g_genet.ext->pwr_mgmt = reg;
    Sleep(1);
    reg &= ~EXT_PHY_RESET;
    g_genet.ext->pwr_mgmt = reg;
}

void UmacReset(void)
{
    g_genet.sys->port_ctrl = PORT_MODE_EXT_GPHY;

    g_genet.sys->rbuf_flush_ctrl |= RBUF_FLUSH_RESET;
    Sleep(1);
    g_genet.sys->rbuf_flush_ctrl &= ~RBUF_FLUSH_RESET;
    Sleep(1);
    g_genet.sys->rbuf_flush_ctrl = 0;
    Sleep(1);

    g_genet.umac->cmd = 0;
    g_genet.umac->cmd = CMD_SW_RESET | CMD_LCL_LOOP_EN;
    Sleep(1);
    g_genet.umac->cmd = 0;

    g_genet.umac->mib_ctrl = MIB_RESET_ALL;
    g_genet.umac->mib_ctrl = 0;

    g_genet.umac->max_frame_len = ENET_MAX_FRAME_LEN;

    g_genet.rbuf->ctrl |= RBUF_ALIGN_2B;
    g_genet.rbuf->tbuf_size_ctrl = 1;
}

uint32_t MibRead(uint32_t offset)
{
    return *(volatile uint32_t *)((uint8_t *)g_genet.umac + offset);
}

void WriteMac(void)
{
    g_genet.umac->mac0 = ((uint32_t)g_genet.mac[0] << 24) | ((uint32_t)g_genet.mac[1] << 16) |
                         ((uint32_t)g_genet.mac[2] << 8) | g_genet.mac[3];
    g_genet.umac->mac1 = ((uint32_t)g_genet.mac[4] << 8) | g_genet.mac[5];
}

void SetLink(uint32_t umac_speed)
{
    uint32_t oob = g_genet.ext->rgmii_oob_ctrl;
    oob &= ~(OOB_DISABLE | ID_MODE_DIS);
    oob |= RGMII_LINK | RGMII_MODE_EN;
    g_genet.ext->rgmii_oob_ctrl = oob;

    uint32_t cmd = g_genet.umac->cmd & ~(3u << CMD_SPEED_SHIFT);
    g_genet.umac->cmd = cmd | (umac_speed << CMD_SPEED_SHIFT);
}
