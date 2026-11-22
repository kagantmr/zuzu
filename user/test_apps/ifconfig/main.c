#include <net/protocols/netd.h>
#include <stdio.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

int main(void)
{
    Handle netd = LookupService("/svc/netd");
    if (netd < 0) {
        printf("ifconfig: can't find /svc/netd (%d)\n", netd);
        return ERR_SYSDOWN;
    }

    uint8_t op = NETD_GET_NETIF;
    MsgWrite(&op, sizeof(op));
    uint32_t got = 0;
    Err rc = CallPort(netd, sizeof(op), -1, &got, NULL);
    if (rc != ZUZU_OK || got < sizeof(NetdNetif)) {
        printf("ifconfig: netd call failed (%d)\n", rc);
        return rc != ZUZU_OK ? rc : ERR_MALFORMED;
    }

    NetdNetif nif;
    memcpy(&nif, GetMessageBox(), sizeof(nif));

    printf("%s\n", nif.name);
    printf("  MAC      %02x:%02x:%02x:%02x:%02x:%02x\n", nif.mac[0], nif.mac[1], nif.mac[2],
           nif.mac[3], nif.mac[4], nif.mac[5]);
    printf("  IP       %u.%u.%u.%u\n", IP4(nif.ip));
    printf("  NETMASK  %u.%u.%u.%u\n", IP4(nif.netmask));
    printf("  GATEWAY  %u.%u.%u.%u\n", IP4(nif.gateway));
    printf("  DNS      %u.%u.%u.%u\n", IP4(nif.dns));
    printf("  LINK     %s\n", nif.link_up ? "up" : "down");
    printf("  RX       %u packets, %u errors, %u oversize, %u ring full, %u overflows\n",
           nif.stat[NIC_STAT_RX_PACKETS], nif.stat[NIC_STAT_RX_ERRORS],
           nif.stat[NIC_STAT_RX_OVERSIZE], nif.stat[NIC_STAT_RX_RING_FULL],
           nif.stat[NIC_STAT_RX_OVERFLOWS]);
    printf("  TX       %u packets, %u drops, %u timeouts, %u underruns\n",
           nif.stat[NIC_STAT_TX_PACKETS], nif.stat[NIC_STAT_TX_DROPS],
           nif.stat[NIC_STAT_TX_TIMEOUTS], nif.stat[NIC_STAT_TX_UNDERRUNS]);
    printf("  IRQ      %u, %u unhandled\n", nif.stat[NIC_STAT_IRQ], nif.stat[NIC_STAT_IRQ_OTHER]);
    printf("  MAC RX   %u bytes, %u bad FCS, %u misaligned, %u runts, %u oversize\n",
           nif.stat[NIC_STAT_MAC_RX_BYTES], nif.stat[NIC_STAT_MAC_RX_FCS],
           nif.stat[NIC_STAT_MAC_RX_ALIGN], nif.stat[NIC_STAT_MAC_RX_RUNT],
           nif.stat[NIC_STAT_MAC_RX_OVERSIZE]);
    printf("  MAC TX   %u bytes\n", nif.stat[NIC_STAT_MAC_TX_BYTES]);
    return 0;
}
