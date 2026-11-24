#include "genet.h"
#include <util/log.h>

static Handle g_rx_buf_h = -1;
static uint8_t *g_rx_buf = NULL;
static uint32_t g_rx_cons = 0;
static uint32_t g_rx_slot = 0;
static uintptr_t g_rx_bus = 0;

Err GenetRxInit(void)
{
    g_genet.rx_ring = (GenetRxRing *)(g_genet.base + GENET_RRING16_OFF);
    g_genet.rx_desc = (GenetDesc *)(g_genet.base + GENET_RDESC_OFF);

    g_rx_buf_h = CreateMem(GENET_BUF_PAGES, MEM_CONTIG);
    if (g_rx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_rx_buf_h));
        return g_rx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet.dev, g_rx_buf_h, 0, GENET_BUF_BYTES, DMA_FROM_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
    g_rx_bus = res.bus_addr;
    void *va = MemMap(g_rx_buf_h, 0, PROT_RW);
    if (PtrIsErr(va)) {
        LOG_ERROR(LOG_TAG, "MemMap of rx buffer failed: %s", StrToError((Err)(VirtAddr)va));
        return (Err)(VirtAddr)va;
    }
    g_rx_buf = va;

    GenetRxRingStart();
    return ZUZU_OK;
}

void GenetRxFree(void)
{
    if (g_rx_buf)
        MemUnmap(g_rx_buf);
    if (g_rx_buf_h >= 0)
        HandleClose(g_rx_buf_h);
    g_rx_buf = NULL;
    g_rx_buf_h = -1;
}

void GenetRxRingStart(void)
{
    GenetRxRing *ring = g_genet.rx_ring;
    uint32_t idx = ring->prod_index & GENET_IDX_MASK;
    uint32_t pos = (idx & (GENET_RING_SIZE - 1)) * GENET_DESC_WORDS;
    g_genet.rdma->scb_burst_size = GENET_DMA_BURST;
    ring->start_addr = 0;
    ring->end_addr = GENET_RING_END_ADDR;
    ring->read_ptr = pos;
    ring->write_ptr = pos;
    ring->cons_index = idx;
    ring->mbuf_done_thresh = GENET_MBUF_DONE_THRESH;
    ring->xon_xoff_thresh = (GENET_FC_LO_THRESH << GENET_FC_LO_SHIFT) | GENET_FC_HI_THRESH;
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.rdma->ring_cfg = DMA_RING16_CFG_EN;

    for (uint32_t i = 0; i < GENET_RING_SIZE; i++) {
        g_genet.rx_desc[i].addr_lo = (uint32_t)g_rx_bus + (i * GENET_BUF_SIZE);
        g_genet.rx_desc[i].addr_hi = 0;
        g_genet.rx_desc[i].length_status = (GENET_BUF_SIZE << DESC_LEN_SHIFT) | DESC_OWN;
    }
    g_rx_cons = idx;
    g_rx_slot = idx & (GENET_RING_SIZE - 1);

    GenetWmb();
    g_genet.rdma->ctrl = DMA_RING16_BUF_EN | DMA_EN;
    g_genet.umac->cmd |= CMD_RX_EN;
}

bool GenetRxPoll(GenetRxFn fn)
{
    uint32_t prod = g_genet.rx_ring->prod_index & GENET_IDX_MASK;
    uint32_t budget = GENET_RX_BUDGET;
    while (g_rx_cons != prod && budget > 0) {
        budget--;
        GenetRmb();
        uint32_t ls = g_genet.rx_desc[g_rx_slot].length_status;
        uint32_t len = (ls >> DESC_LEN_SHIFT) & DESC_LEN_MASK;
        bool whole = (ls & (DESC_SOP | DESC_EOP)) == (DESC_SOP | DESC_EOP);

        if ((ls & DESC_RX_ERRORS) || !whole || len < GENET_RX_PAD + GENET_ETH_HDR_LEN ||
            len > GENET_BUF_SIZE) {
            g_genet.rx_errors++;
            LOG_DEBUG(LOG_TAG, "rx drop: status %04x len %u", ls & 0xffff, len);
        } else if (DmaSync(g_genet.dev, g_rx_bus + (g_rx_slot * GENET_BUF_SIZE),
                           GENET_SYNC_LEN(len), DMA_SYNC_FOR_CPU) != ZUZU_OK) {
            g_genet.rx_errors++;
        } else {
            g_genet.rx_packets++;
            fn(g_rx_buf + (g_rx_slot * GENET_BUF_SIZE) + GENET_RX_PAD, len - GENET_RX_PAD);
        }

        g_rx_slot = (g_rx_slot + 1) % GENET_RING_SIZE;
        g_rx_cons = (g_rx_cons + 1) & GENET_IDX_MASK;
        GenetRmb();
        g_genet.rx_ring->cons_index = g_rx_cons;
    }
    return g_rx_cons != (g_genet.rx_ring->prod_index & GENET_IDX_MASK);
}
