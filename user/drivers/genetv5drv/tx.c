#include "genet.h"
#include <string.h>
#include <util/log.h>

static Handle g_tx_buf_h = -1;
static uint8_t *g_tx_buf = NULL;
static uint32_t g_tx_bus = 0;

Err GenetTxInit(void)
{
    g_tx_buf_h = CreateMem(1, MEM_CONTIG | MEM_UNCACHED);
    if (g_tx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_tx_buf_h));
        return g_tx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet.dev, g_tx_buf_h, 0, 4096, DMA_TO_DEVICE);
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

    g_genet.tx_ring = (GenetTxRing *)(g_genet.base + 0x5000);
    g_genet.tx_desc = (GenetDesc *)(g_genet.base + 0x4000);

    GenetTxRing *ring = g_genet.tx_ring;
    g_genet.tdma->scb_burst_size = 8;
    ring->start_addr = 0;
    ring->read_ptr = 0;
    ring->write_ptr = 0;
    ring->end_addr = (GENET_RING_SIZE * 3) - 1;
    ring->prod_index = ring->cons_index;
    ring->mbuf_done_thresh = 1;
    ring->flow_period = 0;
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.tdma->ring_cfg = 1 << 16;

    g_genet.tdma->ctrl = (1 << 17) | 1;
    g_genet.umac->cmd |= CMD_TX_EN;
    return ZUZU_OK;
}

void GenetTx(const void *frame, uint32_t len)
{
    if (len < 60) {
        memset(g_tx_buf, 0, 60);
        memcpy(g_tx_buf, frame, len);
        len = 60;
    } else {
        memcpy(g_tx_buf, frame, len);
    }

    uint32_t prod = g_genet.tx_ring->prod_index & 0xffff;
    uint32_t n = prod & 0xff;
    g_genet.tx_desc[n].addr_lo = g_tx_bus;
    g_genet.tx_desc[n].addr_hi = 0;
    g_genet.tx_desc[n].length_status = (len << 16) | (0x3fu << 7) | 0x4000u | 0x2000u | 0x0040u;
    g_genet.tx_ring->prod_index = prod + 1;

    Sleep(10);
    LOG_INFO(LOG_TAG, "tx: prod %u cons %u", prod + 1, g_genet.tx_ring->cons_index & 0xffff);
}
