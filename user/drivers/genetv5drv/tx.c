#include "genet.h"
#include <string.h>
#include <util/log.h>
#include <zuzu/uptime.h>

static Handle g_tx_buf_h = -1;
static uint8_t *g_tx_buf = NULL;
static uint32_t g_tx_bus = 0;
static uint32_t g_tx_prod = 0;
static uint32_t g_tx_slot = 0;
static uint32_t g_tx_cons_seen;
static uint64_t g_tx_progress_ms;

Err GenetTxInit(void)
{
    g_tx_buf_h = CreateMem(GENET_BUF_PAGES, MEM_CONTIG);
    if (g_tx_buf_h < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(g_tx_buf_h));
        return g_tx_buf_h;
    }
    DmaMapResult res = DmaMap(g_genet.dev, g_tx_buf_h, 0, GENET_BUF_BYTES, DMA_TO_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(res.status));
        return res.status;
    }
    g_tx_bus = (uint32_t)res.bus_addr;
    void *va = MemMap(g_tx_buf_h, 0, PROT_RW);
    if (PtrIsErr(va)) {
        LOG_ERROR(LOG_TAG, "MemMap of tx buffer failed: %s", StrToError((Err)(VirtAddr)va));
        return (Err)(VirtAddr)va;
    }
    g_tx_buf = va;

    g_genet.tx_ring = (GenetTxRing *)(g_genet.base + GENET_TRING16_OFF);
    g_genet.tx_desc = (GenetDesc *)(g_genet.base + GENET_TDESC_OFF);

    GenetTxRingStart();
    return ZUZU_OK;
}

void GenetTxFree(void)
{
    if (g_tx_buf)
        MemUnmap(g_tx_buf);
    if (g_tx_buf_h >= 0)
        HandleClose(g_tx_buf_h);
    g_tx_buf = NULL;
    g_tx_buf_h = -1;
}

void GenetTxRingStart(void)
{
    GenetTxRing *ring = g_genet.tx_ring;
    uint32_t idx = ring->cons_index & GENET_IDX_MASK;
    uint32_t pos = (idx & (GENET_RING_SIZE - 1)) * GENET_DESC_WORDS;
    g_genet.tdma->scb_burst_size = GENET_DMA_BURST;
    ring->start_addr = 0;
    ring->end_addr = GENET_RING_END_ADDR;
    ring->read_ptr = pos;
    ring->write_ptr = pos;
    ring->prod_index = idx;
    ring->mbuf_done_thresh = GENET_MBUF_DONE_THRESH;
    ring->flow_period = 0;
    ring->ring_buf_size = (GENET_RING_SIZE << 16) | GENET_BUF_SIZE;
    g_genet.tdma->ring_cfg = DMA_RING16_CFG_EN;
    g_tx_prod = idx;
    g_tx_slot = idx & (GENET_RING_SIZE - 1);
    g_tx_cons_seen = idx;
    g_tx_progress_ms = UptimeMs();

    g_genet.tdma->ctrl = DMA_RING16_BUF_EN | DMA_EN;
    g_genet.umac->cmd |= CMD_TX_EN;
}

Err GenetTx(const void *frame, uint32_t len)
{
    if (len > ENET_MAX_FRAME_LEN)
        return ERR_BADARG;
    if (!g_genet.link_up)
        return ERR_NOTCONN;

    uint32_t cons = g_genet.tx_ring->cons_index & GENET_IDX_MASK;
    if (((g_tx_prod - cons) & GENET_IDX_MASK) >= GENET_RING_SIZE) {
        g_genet.tx_busy++;
        return ERR_BUSY;
    }

    if (((g_tx_prod - cons) & GENET_IDX_MASK) == 0)
        g_tx_progress_ms = UptimeMs();

    uint8_t *buf = g_tx_buf + (g_tx_slot * GENET_BUF_SIZE);
    memcpy(buf, frame, len);
    if (len < GENET_MIN_FRAME) {
        memset(buf + len, 0, GENET_MIN_FRAME - len);
        len = GENET_MIN_FRAME;
    }
    Err rc = ZUZU_OK;
    if ((rc = DmaSync(g_genet.dev, g_tx_bus + (g_tx_slot * GENET_BUF_SIZE), GENET_SYNC_LEN(len),
                      DMA_SYNC_FOR_DEVICE)) != ZUZU_OK) {
        return rc;
    }

    GenetDesc *d = &g_genet.tx_desc[g_tx_slot];
    d->addr_lo = g_tx_bus + (g_tx_slot * GENET_BUF_SIZE);
    d->addr_hi = 0;
    d->length_status =
        (len << DESC_LEN_SHIFT) | DESC_TX_QTAG | DESC_EOP | DESC_SOP | DESC_TX_APPEND_CRC;

    g_tx_slot = (g_tx_slot + 1) % GENET_RING_SIZE;
    g_tx_prod = (g_tx_prod + 1) & GENET_IDX_MASK;
    GenetWmb();
    g_genet.tx_ring->prod_index = g_tx_prod;
    g_genet.tx_packets++;
    return ZUZU_OK;
}

void GenetTxReclaim(void)
{
    uint32_t cons = g_genet.tx_ring->cons_index & GENET_IDX_MASK;
    uint32_t done = (cons - g_tx_cons_seen) & GENET_IDX_MASK;
    g_genet.tx_completed += done;
    g_tx_cons_seen = cons;
    if (done > 0)
        g_tx_progress_ms = UptimeMs();
}

bool GenetTxStuck(uint64_t now)
{
    uint32_t pending = (g_tx_prod - g_tx_cons_seen) & GENET_IDX_MASK;
    if (pending == 0 || !g_genet.link_up || now - g_tx_progress_ms <= GENET_TX_TIMEOUT_MS)
        return false;
    g_genet.tx_timeouts++;
    LOG_WARN(LOG_TAG, "tx ring stuck: %u frames pending, cons %u", pending, g_tx_cons_seen);
    g_tx_progress_ms = now;
    return true;
}
