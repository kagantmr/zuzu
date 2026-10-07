#include <net/protocols/netd.h>
#include <stdio.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

int main(void)
{
    Handle netd_port = LookupService("/svc/netd");
    if (netd_port < 0) {
        printf("Couldn't find netd at /svc/netd");
        return ERR_NOSYS;
    }

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, NETD_GET_NETIF);

    SvcResult res = Call(netd_port, 4, -1);
    if (res.r0 != ZUZU_OK) {
        printf("Couldn't find netd at /svc/netd");
        return res.r0;
    }

    NetdNetif netif;
    MsgRead(&netif, sizeof(NetdNetif));

    printf("-------------------------\r\n");
    printf("%s:\r\n", netif.name);
    printf("MAC         %x:%x:%x:%x:%x:%x\r\n", netif.mac[0], netif.mac[1], netif.mac[2],
           netif.mac[3], netif.mac[4], netif.mac[5]);
    printf("NETMASK     %u.%u.%u.%u\r\n", IP4(netif.netmask));
    printf("DNS         %u.%u.%u.%u\r\n", IP4(netif.dns));
    printf("GATEWAY     %u.%u.%u.%u\r\n", IP4(netif.gateway));
    printf("IP(DHCP)    %u.%u.%u.%u\r\n", IP4(netif.ip));

    printf("-------------------------\r\n");

    return 0;
}
