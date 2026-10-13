#include <net/protocols/netd.h>
#include <stdio.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

int main(void)
{
    Handle netd = LookupService("/svc/netd");
    if (netd < 0) {
        printf("ifconfig: can't find /svc/netd (%d)\n", netd);
        return ERR_SYSDOWN;
    }

    uint8_t op = NETD_GET_NETIF;
    MsgWrite(&op, sizeof(op));
    SvcResult res = Call(netd, sizeof(op), -1);
    if (res.r0 != ZUZU_OK || (uint32_t)res.r1 < sizeof(NetdNetif)) {
        printf("ifconfig: netd call failed (%d)\n", (int)res.r0);
        return res.r0 != ZUZU_OK ? (int)res.r0 : ERR_MALFORMED;
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
    return 0;
}
