#include <dev/devm.h>
#include <zuzu/zuzu.h>
#include <zuzu/service.h>
#include <util/log.h>

#define LOG_TAG "pl050drv"
static const char *const DEV_COMPAT[] = {"arm,pl050"};

/**
 * taken from OSDev
 */
typedef struct {
    uint32_t cr;   // control register (rw)
    uint32_t stat; // status register (r)
    uint32_t data; // data register (rw)
    uint32_t clk;  // clock divisor register (rw)
    uint32_t ir;   // interrupt control register (r)
} Pl050Mmio;

static volatile Pl050Mmio *kmi_keybd;
static volatile Pl050Mmio *kmi_mouse;
static Handle g_device_handle = -1;

static void GetDevices(void) {
    Handle devsvc_port = LookupService("/svc/devsvc");
    if (devsvc_port < 0) {
        LOG_ERROR(LOG_TAG, "Couldn't find devsvc");
        return;
    }
    uint32_t matched;
    RequestDevice(devsvc_port, DEV_COMPAT, 1, &matched);
}

int main() {
    return 0;
}