#include "genet.h"
#include <string.h>
#include <util/log.h>

static Handle g_tx_buf_h = -1;
static uint8_t *g_tx_buf = NULL;
static uint32_t g_tx_bus = 0;
static uint32_t g_tx_prod = 0;
static uint32_t g_tx_slot = 0;

Err GenetTxInit(void)
{
    g_tx_buf_h = CreateMem(GENET_BUF_PAGES, MEM_CONTIG | MEM_UNCACHED);
    if (g_tx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_tx_buf_h));
        return g_tx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet.dev, g_tx_buf_h, 0, GENET_BUF_PAGES * 4096, DMA_TO_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
    g_tx_bus = (uint32_t)res.bus_addr;
    g_tx_buf = MemMap(g_tx_buf_h, 0, PROT_RW);
    if (PtrIsErr(g_tx_buf)) {
        LOG_ERROR(LOG_TAG, "MemMap of tx buffer failed: %s", StrToError((Err)(VirtAddr)g_tx_buf));
        return (Err)(VirtAddr)g_tx_buf;
    }

    g_genet.tx_ring = (GenetTxRing *)(g_genet.base + GENET_TRING16_OFF);
    g_genet.tx_desc = (GenetDesc *)(g_genet.base + GENET_TDESC_OFF);

    GenetTxRing *ring = g_genet.tx_ring;
    g_genet.tdma->scb_burst_size = 8;
    ring->start_addr = 0;
    ring->read_ptr = 0;
    ring->write_ptr = 0;
    ring->end_addr = (GENET_RING_SIZE * 3) - 1;
    ring->prod_index = 0;
    ring->cons_index = 0;
    ring->mbuf_done_thresh = 1;
    ring->flow_period = 0;
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.tdma->ring_cfg = DMA_RING16_CFG_EN;
    g_tx_prod = 0;
    g_tx_slot = 0;

    g_genet.tdma->ctrl = DMA_RING16_BUF_EN | DMA_EN;
    g_genet.umac->cmd |= CMD_TX_EN;
    return ZUZU_OK;
}

Err GenetTx(const void *frame, uint32_t len)
{
    if (len > ENET_MAX_FRAME_LEN)
        return ERR_BADARG;
    if (!g_genet.link_up)
        return ERR_NOTCONN;

    uint32_t cons = g_genet.tx_ring->cons_index & 0xffff;
    if (((g_tx_prod - cons) & 0xffff) >= GENET_RING_SIZE) {
        g_genet.tx_busy++;
        return ERR_BUSY;
    }

    uint8_t *buf = g_tx_buf + (g_tx_slot * GENET_BUF_SIZE);
    memcpy(buf, frame, len);
    if (len < GENET_MIN_FRAME) {
        memset(buf + len, 0, GENET_MIN_FRAME - len);
        len = GENET_MIN_FRAME;
    }

    GenetDesc *d = &g_genet.tx_desc[g_tx_slot];
    d->addr_lo = g_tx_bus + (g_tx_slot * GENET_BUF_SIZE);
    d->addr_hi = 0;
    d->length_status =
        (len << DESC_LEN_SHIFT) | DESC_TX_QTAG | DESC_EOP | DESC_SOP | DESC_TX_APPEND_CRC;

    g_tx_slot = (g_tx_slot + 1) % GENET_RING_SIZE;
    g_tx_prod = (g_tx_prod + 1) & 0xffff;
    GenetWmb();
    g_genet.tx_ring->prod_index = g_tx_prod;
    g_genet.tx_packets++;
    return ZUZU_OK;
}
