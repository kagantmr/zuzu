#include "lan9118.h"
#include <dev/protocols/devm.h>
#include <net/packetring.h>
#include <net/protocols/nic.h>
#include <stdio.h>
#include <stdlib.h>
#include <types.h>
#include <util/devices.h>
#include <util/log.h>
#include <zuzu/service.h>
#include <zuzu/syspage.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "lan9118drv"

#define PORT_BIT (0)
#define IRQ_BIT (1)

// on another event object, which is granted over to netd
#define TX_DOORBELL_BIT (0)
#define RX_DOORBELL_BIT (1)

#define PORT_MASK (1u << PORT_BIT)
#define IRQ_MASK (1u << IRQ_BIT)


#define TX_DOORBELL_MASK (1u << TX_DOORBELL_BIT)
#define RX_DOORBELL_MASK (1u << RX_DOORBELL_BIT)

static volatile Lan9118Mmio *nic;
static uint8_t mac[6];
static Handle g_event = -1;
static Handle svc_port = -1;
static Handle shm_tx_handle = -1, shm_rx_handle = -1;
static Handle dev_handle = -1;
static Handle g_doorbell_ev = -1;
static void *shm_tx, *shm_rx;
static NicRing *rx_ring, *tx_ring;

static uint16_t tx_tag = 0;
static uint32_t nic_stats[NIC_STAT_COUNT];

static Handle WaitForDevsvc(void)
{
    for (;;)
    {
        Handle h = LookupService("/svc/devsvc");
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

static void InitPacketRings(void)
{

    shm_tx_handle = CreateMem(NIC_SHM_BYTES); // packet size = 1536, ring_size = 16
    if (shm_tx_handle < 0)
    {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(shm_tx_handle));
        return;
    }
    shm_tx = MemMap(shm_tx_handle, 0, PROT_RW);
    if (PtrIsErr(shm_tx))
    {
        LOG_ERROR(LOG_TAG, "MemMap on shm_tx failed: %s", StrToError((Err)shm_tx));
        return;
    }

    shm_rx_handle = CreateMem(NIC_SHM_BYTES); // packet size = 1536, ring_size = 16
    if (shm_rx_handle < 0)
    {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(shm_rx_handle));
        return;
    }
    shm_rx = MemMap(shm_rx_handle, 0, PROT_RW);
    if (PtrIsErr(shm_rx))
    {
        LOG_ERROR(LOG_TAG, "MemMap on shm_rx failed: %s", StrToError((Err)shm_rx));
        return;
    }

    // create two offsets
    rx_ring = (NicRing *)shm_rx;
    tx_ring = (NicRing *)shm_tx;

    tx_ring->head = 0;
    tx_ring->tail = 0;
    rx_ring->head = 0;
    rx_ring->tail = 0;
}

static void NicTxFrame(NicFrame *f)
{
    if ((nic->tx_fifo_inf & 0xFFFFU) < (f->len + 8U))
    { // +8 for the two command words
        nic_stats[NIC_STAT_TX_DROPS]++;
        return; // tx FIFO full
    }
    uint32_t cmd_a =
        (1u << 13) | (1u << 12) | (f->len & 0x7FF); // first, last, do not interrupt, length
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

static inline uint32_t MacCsrRead(uint8_t index)
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

static inline void MacCsrWrite(uint8_t index, uint32_t value)
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

static Err GetNicHandle(void)
{
    static const char *const nic_compat[] = {"smsc,lan9118"};
    uint32_t matched;
    dev_handle = RequestDevice(WaitForDevsvc(), nic_compat, 1, &matched);
    if (dev_handle < 0)
    {
        LOG_ERROR(LOG_TAG, "RequestDevice failed: %s", StrToError(dev_handle));
        return dev_handle;
    }

    nic = (volatile Lan9118Mmio *)MemMap(dev_handle, 0, PROT_RW);
    if (PtrIsErr((const void *)nic))
    {
        LOG_ERROR(LOG_TAG, "MemMap failed on MMIO: %s", StrToError((Err)nic));
        return ERR_SYSDOWN;
    }

    return ZUZU_OK;
}

static Err Lan9118Setup(void)
{

    if (nic->byte_test != BYTE_TEST_VALUE)
    {
        LOG_ERROR(LOG_TAG, "byte test failed (0x%08X instead of 0x%08x)", nic->byte_test,
                  BYTE_TEST_VALUE);
        return ERR_MALFORMED;
    }

    nic->hw_cfg |= HW_CFG_SRST;
    while (nic->hw_cfg & HW_CFG_SRST)
        ; // poll until clear
    nic->hw_cfg |= HW_CFG_MBO;

    // read MAC addr
    uint32_t lo = MacCsrRead(MAC_CSR_ADDRL);
    uint32_t hi = MacCsrRead(MAC_CSR_ADDRH);
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

    uint32_t mac_cr = MacCsrRead(MAC_CSR_MAC_CR);
    MacCsrWrite(MAC_CSR_MAC_CR, mac_cr | MAC_CR_RXEN | MAC_CR_TXEN);

    nic->irq_cfg = IRQ_CFG_IRQ_EN | IRQ_CFG_IRQ_POL | IRQ_CFG_IRQ_TYPE;
    nic->int_en = INT_RSFL;

    LOG_INFO(LOG_TAG, "NIC setup OK");

    return ZUZU_OK;
}

void InitLan9118Svcs(void)
{
    Err rc;

    svc_port = CreatePort();
    if (svc_port < 0)
    {
        LOG_ERROR(LOG_TAG, "CreatePort failed: %s", StrToError(svc_port));
        return;
    }

    g_event = CreateEvent();
    if (g_event < 0)
    {
        LOG_ERROR(LOG_TAG, "CreateEvent failed: %s", StrToError(g_event));
        return;
    }

    rc = BindIrq(g_event, dev_handle, IRQ_BIT);
    if (rc < 0)
    {
        LOG_ERROR(LOG_TAG, "BindIrq failed: %s", StrToError(rc));
        return;
    }
    
    g_doorbell_ev = CreateEvent();
    if (g_doorbell_ev < 0)
    {
        LOG_ERROR(LOG_TAG, "tx doorbell registration failed");
        return;
    }

    rc = Bind(EVENT_PORT, g_event, svc_port, PORT_BIT);
    if (rc < 0)
    {
        LOG_ERROR(LOG_TAG, "Bind failed: %s", StrToError(rc));
        return;
    }

    rc = RegisterService("/dev/eth0", svc_port);
    if (rc < 0)
    {
        LOG_ERROR(LOG_TAG, "service registration failed: %s", StrToError(rc));
        return;
    }
}

int main(void)
{
    int rc1 = GetNicHandle();
    if (rc1 != 0)
        return rc1;

    int rc2 = Lan9118Setup();
    if (rc2 != 0)
        return rc2;

    InitPacketRings();
    InitLan9118Svcs();

    for (;;)
    {
    }

    return 0;
}
