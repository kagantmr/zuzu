#include "dev/devm.h"
#include "genetv5.h"
#include <util/log.h>
#include <zuzu/service.h>
#include <zuzu/syspage.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "genetv5drv"

#define UDBG_ENABLED 1

#include <stdio.h>
#include <zuzu/udbg.h>

static const char *compats[1] = {"brcm,bcm2711-genet-v5"};

static Handle g_genet_h = -1;
static void *genet_base = NULL;
static GenetSys *genet_sys = NULL;
static GenetUmac *genet_umac = NULL;
static uint8_t mac[6];

static Handle WaitForService(const char *path)
{
    for (;;) {
        Handle h = LookupService(path);
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

int main()
{
    uint32_t matched;
    g_genet_h = RequestDevice(WaitForService("/svc/devsvc"), compats, 1, &matched);
    if (g_genet_h < 0) {
        LOG_INFO(LOG_TAG, "RequestDevice failed: %s", StrToError(g_genet_h));
        return g_genet_h;
    }

    genet_base = MemMap(g_genet_h, 0, PROT_RW);
    if (PtrIsErr(genet_base)) {
        LOG_INFO(LOG_TAG, "MemMap failed: %s", StrToError((Err)(VirtAddr)genet_base));
        return (Err)(VirtAddr)genet_base;
    }

    genet_sys = (GenetSys *)(genet_base + 0x000);
    uint32_t rev = genet_sys->rev_ctrl;
    LOG_INFO(LOG_TAG, "genetv%u\n", ((rev >> 24) & 0xf) - 1);

    genet_umac = (GenetUmac *)(genet_base + 0x800);

    mac[0] = genet_umac->mac0 >> 24;   
    mac[1] = genet_umac->mac0 >> 16;   
    mac[2] = genet_umac->mac0 >> 8;   
    mac[3] = genet_umac->mac0;
    mac[4] = genet_umac->mac1 >> 8;    
    mac[5] = genet_umac->mac1;

    LOG_INFO(LOG_TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return ZUZU_OK;
}
