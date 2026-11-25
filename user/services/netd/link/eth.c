#include "eth.h"
#include "../common/globals.h"
#include "../net/ip.h"
#include "arp.h"
#include <net/protocols/nic.h>
#include <stdio.h>
#include "../common/netif.h"
#include <stdlib.h>
#include <string.h>
#include <zuzu/zuzu.h>

int EthRx(uint8_t *data, uint16_t len)
{
    EthHdr *hdr = (EthHdr *)data;
    if (len < 14) {
        return ERR_MALFORMED;
    }
    uint16_t et = ntohs(hdr->ethertype);
    switch (et) {
    case ETH_TYPE_ARP: {
        ArpRx(data + 14, len - 14);
        break;
    }
    case ETH_TYPE_IP: {
        if (len < sizeof(EthHdr) + sizeof(IpHeader)) {
            return ERR_MALFORMED;
        }
        IpRx(data + 14, len - 14, hdr->src_mac);
        break;
    }
    default: {
        return ERR_MALFORMED;
    }
    }

    return ZUZU_OK;
}

int EthSendFrame(TxFrame *f, MacAddr dst_mac, uint16_t ethertype)
{
    EthHdr *hdr = (EthHdr *)TxFramePrepend(f, sizeof(EthHdr));
    if (!hdr)
        return ERR_OVERFLOW; /* slot abandoned (uncommitted) */

    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, f->nif->mac, 6);
    hdr->ethertype = htons(ethertype);

    /* Header lands at slot->data[0], the whole frame is now contiguous. */
    f->slot->len = TxFrameLen(f);
    PacketRingCommit(f->nif->drv.tx_ring);
    return Signal(f->nif->drv.doorbell, NIC_DOORBELL_TX, false);
}
