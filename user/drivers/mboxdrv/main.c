#include <arch/barrier.h>
#include <dev/devm.h>
#include <dev/protocols/mbox.h>
#include <stdint.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "mboxdrv"

#define MBOX_REG_OFFSET 0x880u

#define MBOX0_READ 0x00u
#define MBOX0_STATUS 0x18u
#define MBOX1_WRITE 0x20u
#define MBOX1_STATUS 0x38u

#define MBOX_FULL 0x80000000u
#define MBOX_EMPTY 0x40000000u

#define MBOX_CH_PROPERTY 8u

#define MBOX_REQUEST 0x00000000u
#define MBOX_RESP_OK 0x80000000u
#define MBOX_TAG_RESP 0x80000000u

#define VC_BUS_ALIAS 0xC0000000u
#define VC_RAM_LIMIT 0x3C000000u

#define MBOX_SPIN_LIMIT 10000000u

static const char *const mbox_compat[] = {"brcm,bcm2835-mbox"};

static volatile uint8_t *g_regs;
static volatile uint32_t *g_buf;
static uint32_t g_buf_bus;
static Handle g_port = -1;

static inline uint32_t RegRead(uint32_t off) { return *(volatile uint32_t *)(g_regs + off); }

static inline void RegWrite(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_regs + off) = v; }

static Handle WaitForService(const char *path)
{
    for (;;) {
        Handle h = LookupService(path);
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

static Err MboxCall(void)
{
    uint32_t msg = g_buf_bus | MBOX_CH_PROPERTY;

    DsbSy();
    uint32_t spins = 0;
    while (RegRead(MBOX1_STATUS) & MBOX_FULL) {
        if (++spins > MBOX_SPIN_LIMIT)
            return ERR_TIMEOUT;
    }
    RegWrite(MBOX1_WRITE, msg);

    for (;;) {
        spins = 0;
        while (RegRead(MBOX0_STATUS) & MBOX_EMPTY) {
            if (++spins > MBOX_SPIN_LIMIT)
                return ERR_TIMEOUT;
        }
        uint32_t got = RegRead(MBOX0_READ);
        if (got == msg)
            break;
        LOG_WARN(LOG_TAG, "dropping stray reply 0x%08x", got);
    }
    DsbSy();
    return ZUZU_OK;
}

static Err RunTag(uint32_t tag, const uint8_t *in, uint32_t buf_len, uint32_t req_len,
                  uint8_t *out, uint32_t *resp_len)
{
    uint32_t value_words = (buf_len + 3u) / 4u;
    uint32_t total_words = 2u + 3u + value_words + 1u;

    g_buf[0] = total_words * 4u;
    g_buf[1] = MBOX_REQUEST;
    g_buf[2] = tag;
    g_buf[3] = value_words * 4u;
    g_buf[4] = req_len;
    for (uint32_t i = 0; i < value_words; i++) {
        uint32_t w = 0;
        for (uint32_t b = 0; b < 4u; b++) {
            uint32_t idx = i * 4u + b;
            if (idx < req_len)
                w |= (uint32_t)in[idx] << (8u * b);
        }
        g_buf[5u + i] = w;
    }
    g_buf[5u + value_words] = 0;

    Err rc = MboxCall();
    if (rc != ZUZU_OK)
        return rc;

    if (g_buf[1] != MBOX_RESP_OK || !(g_buf[4] & MBOX_TAG_RESP)) {
        LOG_WARN(LOG_TAG, "tag 0x%08x refused: code 0x%08x tag 0x%08x", tag, g_buf[1], g_buf[4]);
        return ERR_IO;
    }

    uint32_t len = g_buf[4] & ~MBOX_TAG_RESP;
    uint32_t n = len < buf_len ? len : buf_len;
    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t)(g_buf[5u + i / 4u] >> (8u * (i % 4u)));
    *resp_len = len;
    return ZUZU_OK;
}

static void HandleRequest(const PortWaitResult *r)
{
    if (r->granted >= 0)
        HandleClose(r->granted);

    static MboxRequest req;
    static MboxReply rep;
    memset(&rep, 0, sizeof(rep));

    if (r->xlen < offsetof(MboxRequest, value) || r->xlen > sizeof(req)) {
        rep.status = ERR_BADARG;
    } else {
        memset(&req, 0, sizeof(req));
        memcpy(&req, GetMessageBox(), r->xlen);
        if (req.cmd != MBOX_CMD_PROPERTY)
            rep.status = ERR_NOSYS;
        else if (req.buf_len > MBOX_VALUE_MAX || req.req_len > req.buf_len ||
                 r->xlen < offsetof(MboxRequest, value) + req.req_len)
            rep.status = ERR_BADARG;
        else
            rep.status = RunTag(req.tag, req.value, req.buf_len, req.req_len, rep.value,
                                &rep.resp_len);
    }

    uint32_t n = rep.resp_len < MBOX_VALUE_MAX ? rep.resp_len : MBOX_VALUE_MAX;
    uint32_t len = (uint32_t)offsetof(MboxReply, value) + (rep.status == ZUZU_OK ? n : 0u);
    memcpy(GetMessageBox(), &rep, len);
    Reply(len, -1);
}

static Err MboxInit(void)
{
    Handle dev = RequestDevice(WaitForService("/svc/devsvc"), mbox_compat, 1, NULL);
    if (dev < 0) {
        LOG_ERROR(LOG_TAG, "RequestDevice failed: %s", StrToError(dev));
        return dev;
    }

    void *mmio = MemMap(dev, 0, PROT_RW);
    if (PtrIsErr(mmio)) {
        LOG_ERROR(LOG_TAG, "MemMap of registers failed: %s", StrToError((Err)(VirtAddr)mmio));
        return (Err)(VirtAddr)mmio;
    }
    g_regs = (volatile uint8_t *)mmio + MBOX_REG_OFFSET;

    Handle mem = CreateMem(1, MEM_CONTIG | MEM_UNCACHED);
    if (mem < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(mem));
        return mem;
    }
    void *buf = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(buf)) {
        LOG_ERROR(LOG_TAG, "MemMap of buffer failed: %s", StrToError((Err)(VirtAddr)buf));
        return (Err)(VirtAddr)buf;
    }
    g_buf = (volatile uint32_t *)buf;

    DmaMapResult m = DmaMap(dev, mem, 0, 4096, DMA_BIDIRECTIONAL);
    if (m.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %s", StrToError(m.status));
        return m.status;
    }
    if (m.bus_addr >= VC_RAM_LIMIT) {
        LOG_ERROR(LOG_TAG, "buffer at 0x%lx is out of VideoCore reach", (unsigned long)m.bus_addr);
        return ERR_NOMEM;
    }
    g_buf_bus = (uint32_t)m.bus_addr | VC_BUS_ALIAS;

    uint8_t mac[8];
    uint32_t len = 0;
    Err rc = RunTag(MBOX_TAG_GET_MAC, NULL, sizeof(mac), 0, mac, &len);
    if (rc == ZUZU_OK)
        LOG_INFO(LOG_TAG, "board MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    else
        LOG_WARN(LOG_TAG, "MAC query failed: %s", StrToError(rc));

    g_port = CreatePort();
    if (g_port < 0) {
        LOG_ERROR(LOG_TAG, "CreatePort failed: %s", StrToError(g_port));
        return g_port;
    }
    return RegisterService(MBOX_SERVICE, g_port);
}

int main(void)
{
    Err rc = MboxInit();
    if (rc != ZUZU_OK)
        return rc;

    for (;;) {
        PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK)
            continue;
        HandleRequest(&r);
    }
}
