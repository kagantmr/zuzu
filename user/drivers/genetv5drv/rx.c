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
    DmaMapResult res = DmaMap(g_genet.dev, g_rx_buf_h, 0, GENET_BUF_PAGES * 4096, DMA_FROM_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
    g_rx_bus = res.bus_addr;
    g_rx_buf = MemMap(g_rx_buf_h, 0, PROT_RW);
    if (PtrIsErr(g_rx_buf)) {
        LOG_ERROR(LOG_TAG, "MemMap of rx buffer failed: %s", StrToError((Err)(VirtAddr)g_rx_buf));
        return (Err)(VirtAddr)g_rx_buf;
    }

    GenetRxRing *ring = g_genet.rx_ring;
    g_genet.rdma->scb_burst_size = 8;
    ring->start_addr = 0;
    ring->read_ptr = 0;
    ring->write_ptr = 0;
    ring->end_addr = (GENET_RING_SIZE * 3) - 1;
    ring->prod_index = 0;
    ring->cons_index = 0;
    ring->mbuf_done_thresh = 1;
    ring->xon_xoff_thresh = (5 << 16) | (GENET_RING_SIZE >> 4);
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.rdma->ring_cfg = DMA_RING16_CFG_EN;

    for (uint32_t i = 0; i < GENET_RING_SIZE; i++) {
        g_genet.rx_desc[i].addr_lo = (uint32_t)res.bus_addr + (i * GENET_BUF_SIZE);
        g_genet.rx_desc[i].addr_hi = 0;
        g_genet.rx_desc[i].length_status = (GENET_BUF_SIZE << DESC_LEN_SHIFT) | DESC_OWN;
    }
    g_rx_cons = 0;
    g_rx_slot = 0;

    GenetWmb();
    g_genet.rdma->ctrl = DMA_RING16_BUF_EN | DMA_EN;
    g_genet.umac->cmd |= CMD_RX_EN;
    return ZUZU_OK;
}

void GenetRxPoll(GenetRxFn fn)
{
    uint32_t prod = g_genet.rx_ring->prod_index & 0xffff;
    while (g_rx_cons != prod) {
        GenetRmb();
        uint32_t ls = g_genet.rx_desc[g_rx_slot].length_status;
        uint32_t len = (ls >> DESC_LEN_SHIFT) & DESC_LEN_MASK;
        bool whole = (ls & (DESC_SOP | DESC_EOP)) == (DESC_SOP | DESC_EOP);

        if ((ls & DESC_RX_ERRORS) || !whole || len < 2 + 14 || len > GENET_BUF_SIZE) {
            g_genet.rx_errors++;
            LOG_DEBUG(LOG_TAG, "rx drop: status %04x len %u", ls & 0xffff, len);
        } else if (DmaSync(g_genet.dev, g_rx_bus + (g_rx_slot * GENET_BUF_SIZE), (len + 63) & ~63u,
                           DMA_SYNC_FOR_CPU) != ZUZU_OK) {
            g_genet.rx_errors++;
        } else {
            g_genet.rx_packets++;
            fn(g_rx_buf + (g_rx_slot * GENET_BUF_SIZE) + 2, len - 2);
        }

        g_rx_slot = (g_rx_slot + 1) % GENET_RING_SIZE;
        g_rx_cons = (g_rx_cons + 1) & 0xffff;
        GenetRmb();
        g_genet.rx_ring->cons_index = g_rx_cons;
    }
}
