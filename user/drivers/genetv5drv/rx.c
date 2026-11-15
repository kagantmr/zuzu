#include "genet.h"
#include <util/log.h>

static Handle g_rx_buf_h = -1;
static uint8_t *g_rx_buf = NULL;
static uint32_t g_rx_cons = 0;

Err GenetRxInit(void)
{
    g_genet.rx_ring = (GenetRxRing *)(g_genet.base + 0x3000);
    g_genet.rx_desc = (GenetDesc *)(g_genet.base + 0x2000);

    g_rx_buf_h = CreateMem(128, MEM_CONTIG | MEM_UNCACHED);
    if (g_rx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_rx_buf_h));
        return g_rx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet.dev, g_rx_buf_h, 0, 4096 * 128, DMA_FROM_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
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
    ring->cons_index = ring->prod_index;
    ring->mbuf_done_thresh = 1;
    ring->xon_xoff_thresh = (5 << 16) | (GENET_RING_SIZE >> 4);
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.rdma->ring_cfg = 1 << 16;

    for (int i = 0; i < GENET_RING_SIZE; i++) {
        g_genet.rx_desc[i].addr_lo = (uint32_t)res.bus_addr + (i * GENET_BUF_SIZE);
        g_genet.rx_desc[i].addr_hi = 0;
        g_genet.rx_desc[i].length_status = (GENET_BUF_SIZE << 16) | 0x8000;
    }

    g_genet.rdma->ctrl = (1 << 17) | 1;
    g_genet.umac->cmd |= CMD_RX_EN;

    g_rx_cons = ring->cons_index & 0xffff;
    return ZUZU_OK;
}

void GenetRxPoll(void)
{
    uint32_t prod = g_genet.rx_ring->prod_index & 0xffff;
    while (g_rx_cons != prod) {
        uint32_t i = g_rx_cons & 0xff;
        uint32_t ls = g_genet.rx_desc[i].length_status;
        uint32_t len = (ls >> 16) & 0xfff;
        uint8_t *f = g_rx_buf + (i * GENET_BUF_SIZE) + 2;
        LOG_INFO(LOG_TAG, "rx %u bytes from %02x:%02x:%02x:%02x:%02x:%02x type %02x%02x flags %04x",
                 len - 2, f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], ls & 0xffff);
        g_rx_cons = (g_rx_cons + 1) & 0xffff;
        g_genet.rx_ring->cons_index = g_rx_cons;
    }
}
