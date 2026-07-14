#include <stdio.h>
#include <zuzu/protocols/devmgr_protocol.h>
#include <zuzu/protocols/nic_protocol.h>
#include <zuzu/msg.h>
#include <zuzu/irq.h>
#include <zuzu/ntfn.h>
#include <zuzu/task.h>
#include <zuzu/umem.h>
#include <zuzu/types.h>
#include <zuzu/syspage.h>
#include <zuzu/devices.h>
#include <zuzu/service.h>
#include <zuzu/log.h>
#include <stdlib.h>
#include <zuzu/packetring.h>
#include "lan9118.h"

#define LOG_TAG "lan9118drv"

static volatile lan9118_t *nic;
static uint8_t mac[6];
static handle_t irq_ntfn;
static handle_t shmem_handle;
static handle_t nt_port, devm_port;
static handle_t dev_handle;
static handle_t netd_ntfn;
static handle_t tx_doorbell_ntfn;
void *shmem_addr;
nic_ring_t *rx_ring, *tx_ring;

static uint16_t tx_tag = 0;
static uint32_t nic_stats[NIC_STAT_COUNT];

void packet_ring_init(void *shm) {
    // create two offsets
    rx_ring = (nic_ring_t *)((uint8_t *)shm + NIC_RX_OFFSET);
    tx_ring = (nic_ring_t *)((uint8_t *)shm + NIC_TX_OFFSET);

    tx_ring->head = 0;
    tx_ring->tail = 0;
    rx_ring->head = 0;
    rx_ring->tail = 0;
}

void nic_tx_frame(nic_frame_t *f)
{
    if ((nic->tx_fifo_inf & 0xFFFFu) < ((uint32_t)f->len + 8u)) { // +8 for the two command words
        nic_stats[NIC_STAT_TX_DROPS]++;
        return;                                                  // tx FIFO full
    }
    uint32_t cmd_a = (1u << 13) | (1u << 12) | (f->len & 0x7FF); // first, last, do not interrupt, length
    uint32_t cmd_b = ((uint32_t)tx_tag << 16) | (f->len & 0x7FF);
    tx_tag++;
    nic->tx_data_fifo_port = cmd_a;
    nic->tx_data_fifo_port = cmd_b;
    for (size_t i = 0; i < ((size_t)(f->len + 3) / 4); i++)
    {
        nic->tx_data_fifo_port = ((uint32_t *)f->data)[i];
    }
    nic_stats[NIC_STAT_TX_PACKETS]++;
}

static inline uint32_t mac_csr_read(uint8_t index)
{
    // write index with read bit
    while (nic->mac_csr_cmd & MAC_CSR_CMD_BUSY)
        ;
    nic->mac_csr_cmd = MAC_CSR_CMD_BUSY | MAC_CSR_CMD_RNW | (index & MAC_CSR_CMD_ADDR_MASK);

    // wait for read to complete
    while (nic->mac_csr_cmd & MAC_CSR_CMD_BUSY)
        ;
    return nic->mac_csr_data;
}

static inline void mac_csr_write(uint8_t index, uint32_t value)
{
    // write index with write bit
    while (nic->mac_csr_cmd & MAC_CSR_CMD_BUSY)
        ;
    nic->mac_csr_data = value;
    nic->mac_csr_cmd = MAC_CSR_CMD_BUSY | (index & MAC_CSR_CMD_ADDR_MASK);

    // wait for write to complete
    while (nic->mac_csr_cmd & MAC_CSR_CMD_BUSY)
        ;
}

int get_nic(void)
{

    if (!device_present("LAN9118"))
    {
        LOG_ERROR(LOG_TAG, "device not found");
        return ERR_NOENT;
    }
    // service is registered after nic_setup() completes so clients don't
    // find the port before _waitany is running
    nt_port = -1;

    devm_port = lookup_service("devm");
    msg_t r;
    while (1)
    {
        r = _call(devm_port, DEV_REQUEST, DEV_CLASS_NIC, 0);
        if ((int32_t)r.r1 == 0)
            break;
        LOG_WARN(LOG_TAG, "NIC device request failed, retrying");
        _sleep(10);
    }
    dev_handle = (handle_t)r.r2;
    irq_ntfn = _ntfn_create();
    if (irq_ntfn < 0)
    {
        LOG_ERROR(LOG_TAG, "ntfn_create failed (tx)");
        return ERR_SYSDOWN;
    }

    _irq_claim((uint32_t)dev_handle);
    _irq_bind((uint32_t)dev_handle, (uint32_t)irq_ntfn);

    nic = (volatile lan9118_t *)_memmap(dev_handle, 0, VM_PROT_RW, 0);
    if (!nic)
    {
        LOG_ERROR(LOG_TAG, "device mapping failed");
        return ERR_SYSDOWN;
    }

    return ZUZU_OK;
}

int nic_setup(void)
{

    if (nic->byte_test != BYTE_TEST_VALUE)
    {
        LOG_ERROR(LOG_TAG, "byte test failed (0x%08X instead of 0x%08x)", nic->byte_test, BYTE_TEST_VALUE);
        return ERR_MALFORMED;
    }

    nic->hw_cfg |= HW_CFG_SRST;
    while (nic->hw_cfg & HW_CFG_SRST)
        ; // poll until clear
    nic->hw_cfg |= HW_CFG_MBO;

    // read MAC addr
    uint32_t lo = mac_csr_read(MAC_CSR_ADDRL);
    uint32_t hi = mac_csr_read(MAC_CSR_ADDRH);
    mac[0] = (lo >> 0) & 0xFF;
    mac[1] = (lo >> 8) & 0xFF;
    mac[2] = (lo >> 16) & 0xFF;
    mac[3] = (lo >> 24) & 0xFF;
    mac[4] = (hi >> 0) & 0xFF;
    mac[5] = (hi >> 8) & 0xFF;

    if (nic->tx_cfg & TX_CFG_STOP_TX)
    {
        LOG_ERROR(LOG_TAG, "TX is stopped");
        return ERR_SYSDOWN;
    }

    nic->tx_cfg |= TX_CFG_TXS_DUMP | TX_CFG_TXD_DUMP;
    while ((nic->tx_cfg & (TX_CFG_TXS_DUMP | TX_CFG_TXD_DUMP)))
        ;
    nic->rx_cfg |= RX_CFG_RX_DUMP;
    while (nic->rx_cfg & RX_CFG_RX_DUMP)
        ;
    nic->tx_cfg |= TX_CFG_TX_ON;

    uint32_t mac_cr = mac_csr_read(MAC_CSR_MAC_CR);
    mac_csr_write(MAC_CSR_MAC_CR, mac_cr | MAC_CR_RXEN | MAC_CR_TXEN);
    // Configure active-high push-pull interrupts so QEMU doesn't permanently
    // invert the signal. With default irq_cfg=0 QEMU inverts level, making
    // the GIC line always asserted regardless of int_en.
    nic->irq_cfg = IRQ_CFG_IRQ_EN | IRQ_CFG_IRQ_POL | IRQ_CFG_IRQ_TYPE;
    nic->int_en = INT_RSFL;

    LOG_INFO(LOG_TAG, "NIC setup OK");

    // finally, set up shmem

    shmem_handle = _shm_create(NIC_SHM_BYTES); // packet size = 1536, ring_size = 16
    if (shmem_handle < 0) {
        LOG_ERROR(LOG_TAG, "shmem create failed");
        return ERR_SYSDOWN;
    }
    shmem_addr = _memmap(shmem_handle, 0, VM_PROT_RW, 0);
    if (_ptr_is_err(shmem_addr)) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return ERR_SYSDOWN;
    }

    packet_ring_init(shmem_addr);

    netd_ntfn = _ntfn_create();
    if (netd_ntfn < 0) {
        LOG_ERROR(LOG_TAG, "notification registration failed");
        return ERR_SYSDOWN;
    }

    tx_doorbell_ntfn = _ntfn_create();
    if (tx_doorbell_ntfn < 0) {
        LOG_ERROR(LOG_TAG, "tx doorbell registration failed");
        return ERR_SYSDOWN;
    }

    return ZUZU_OK;
}

void lan9118_service_loop(void)
{
    nt_port = register_service("nic0");
    if (nt_port < 0)
    {
        LOG_ERROR(LOG_TAG, "service registration failed");
        return;
    }

    enum { H_PORT = 0, H_IRQ = 1, H_TXDOORBELL = 2 };
    handle_t handles[] = {
        [H_PORT] = nt_port,
        [H_IRQ] = irq_ntfn,
        [H_TXDOORBELL] = tx_doorbell_ntfn,
    };
    handle_t pending_recv_reply = 0;

    while (1)
    {

        waitany_result_t result;
        int32_t recv_rc = _waitany(handles, 3, 50, &result);
        if (recv_rc < 0)
        {
            continue;
        }

        switch (result.kind)
        {
        case WAITANY_KIND_NTFN:
        {
            if (result.matched_index == H_TXDOORBELL)
            {
                /* netd queued frames in tx_ring: drain them straight from the
                   shared slots into the FIFO (zero-copy, batched). */
                nic_frame_t *slot;
                while ((slot = packet_ring_peek(tx_ring)) != NULL)
                {
                    nic_tx_frame(slot);
                    packet_ring_consume(tx_ring);
                }
                break;
            }

            nic_stats[NIC_STAT_IRQ]++;
            uint32_t sts = nic->int_sts;
            nic->int_sts = sts; // write back to clear R/WC bits
            if (sts & INT_RSFL)
            {
                /* drain RX FIFO */
                while ((nic->rx_fifo_inf >> 16) & 0xFF)
                {
                    uint32_t rx_sts = nic->rx_status_fifo_port;
                    size_t pkt_len = (rx_sts >> 16) & 0x3FFF;
                    if (rx_sts & (1u << 15))
                    {
                        nic_stats[NIC_STAT_RX_ERRORS]++;
                        uint32_t dwords = (pkt_len + 3) / 4;
                        for (uint32_t i = 0; i < dwords; i++)
                            (void)nic->rx_data_fifo_port;
                    }
                    else
                    {
                        _Alignas(4) uint8_t buf[NIC_FRAME_SIZE];
                        if (pkt_len > NIC_FRAME_SIZE)
                        {
                            nic_stats[NIC_STAT_RX_OVERSIZE]++;
                            uint32_t dwords = (pkt_len + 3) / 4;
                            for (uint32_t i = 0; i < dwords; i++)
                                (void)nic->rx_data_fifo_port;
                            continue;
                        }
                        uint32_t dwords = (pkt_len + 3) / 4;
                        for (uint32_t i = 0; i < dwords; i++)
                            ((uint32_t *)buf)[i] = nic->rx_data_fifo_port;
                        int push_rc = packet_ring_push(rx_ring, buf, pkt_len);
                        if (push_rc < 0)
                        {
                            nic_stats[NIC_STAT_RX_RING_FULL]++;
                            continue;
                        }
                        nic_stats[NIC_STAT_RX_PACKETS]++;
                        if (pending_recv_reply > 0)
                        {
                            _reply(pending_recv_reply, ZUZU_OK, (uint32_t)pkt_len, 0);
                            pending_recv_reply = 0;
                        }
                        _ntfn_signal(netd_ntfn, 1);
                    }
                }
            }
            if (sts & INT_TSFL)
            {
                /* drain TX status FIFO */
                while ((nic->tx_fifo_inf >> 16) & 0xFF)
                {
                    uint32_t tx_sts = nic->tx_status_fifo_port;
                    (void)tx_sts;
                }
            }

            _irq_done(dev_handle);
            break;
        }
        case WAITANY_KIND_CALL:
        {
            switch (result.r2)
            {
            case NIC_CMD_GETMAC:
            {
                /* Status goes in r1 so the MAC bytes in r2/r3 can never be
                   misread as an error (a high 4th octet makes mac_lo negative). */
                int32_t status = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])
                                     ? ZUZU_OK
                                     : ERR_SYSDOWN; // MAC never read -> treat as system down
                _reply(result.source, status,
                       (mac[0] | mac[1] << 8 | mac[2] << 16 | mac[3] << 24),
                       (mac[4] | mac[5] << 8));
                break;
            }
            case NIC_CMD_GETBUF:
            {
                if (shmem_handle < 0 || shmem_addr == NULL)
                {
                    _reply(result.source, ERR_SYSDOWN, 0, 0);
                }
                else
                {
                    /* r1 = shmem, r2 = rx doorbell, r3 = tx doorbell */
                    int32_t shm_g = _grant(shmem_handle, result.r1);
                    int32_t rx_g  = _grant(netd_ntfn, result.r1);
                    int32_t tx_g  = _grant(tx_doorbell_ntfn, result.r1);
                    if (shm_g < 0 || rx_g < 0 || tx_g < 0)
                        _reply(result.source, ERR_SYSDOWN, 0, 0);
                    else
                        _reply(result.source, shm_g, rx_g, tx_g);
                }
                break;
            }
            case NIC_CMD_SEND:
            {
                nic_frame_t frame;
                while (packet_ring_pop(&frame, tx_ring) == 0)
                    nic_tx_frame(&frame);
                _reply(result.source, 0, ZUZU_OK, 0);
                break;
            }
            case NIC_CMD_RECV:
            {
                if (rx_ring->head != rx_ring->tail)
                    _reply(result.source, 0, ZUZU_OK, 0);
                else
                    pending_recv_reply = result.source;
                break;
            }
            case NIC_CMD_STATS:
            {
                uint32_t idx = result.r3; // NIC_STAT_* selector
                if (idx < NIC_STAT_COUNT)
                    _reply(result.source, 0, nic_stats[idx], NIC_STAT_COUNT);
                else
                    _reply(result.source, ERR_BADARG, 0, NIC_STAT_COUNT);
                break;
            }
            default:
                _reply(result.source, 0, ERR_NOMATCH, 0);
                break;
            }
            break;
        }
        case WAITANY_KIND_TIMEOUT:
        {
            /* Surface loss when the link goes idle, only when it changed, so
               bursts don't spam but drops never stay silent. */
            static uint32_t last_drops = 0;
            uint32_t drops = nic_stats[NIC_STAT_RX_RING_FULL] +
                             nic_stats[NIC_STAT_RX_ERRORS] +
                             nic_stats[NIC_STAT_RX_OVERSIZE] +
                             nic_stats[NIC_STAT_TX_DROPS];
            if (drops != last_drops)
            {
                last_drops = drops;
                LOG_WARN(LOG_TAG,
                         "drops rx_ringfull=%u rx_err=%u rx_oversize=%u tx=%u "
                         "(ok rx=%u tx=%u irq=%u)",
                         nic_stats[NIC_STAT_RX_RING_FULL], nic_stats[NIC_STAT_RX_ERRORS],
                         nic_stats[NIC_STAT_RX_OVERSIZE], nic_stats[NIC_STAT_TX_DROPS],
                         nic_stats[NIC_STAT_RX_PACKETS], nic_stats[NIC_STAT_TX_PACKETS],
                         nic_stats[NIC_STAT_IRQ]);
            }
            break;
        }
        default:
            __builtin_unreachable();
        }
    }
}

int main(void)
{
    int rc1 = get_nic();
    if (rc1 != 0)
        return rc1;

    int rc2 = nic_setup();
    if (rc2 != 0)
        return rc2;

    lan9118_service_loop();

    __builtin_unreachable();
}
