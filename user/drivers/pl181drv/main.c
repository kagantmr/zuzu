#include "dev/protocols/devm.h"
#include "dev/protocols/mmcdrv.h"
#include "pl181drv.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "pl181drv"
#define LOG_INFO(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)

#define BIT_IRQ 0
#define BIT_PORT 1
#define MASK(bit) (1U << (bit))
#define POLL_MS 10 /* bits are hints: STATUS is re-checked at least this often */
#define XFER_TIMEOUT_POLLS 500

static Pl181Mmio *pl181;
static bool is_sdhc;

static Handle port = -1;
static Handle block_dev_handle = -1;
static Handle g_event = -1;
static Handle g_buf_mem = -1;
static uint32_t *shmem_buf = NULL;

static void Pl181BusySpin(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++)
        __asm__ volatile("nop");
}

static int Pl181IssueCmd(uint32_t cmd, uint32_t arg, uint32_t flags)
{
    const uint32_t clear_mask = MCI_CMDCRCFAIL | MCI_CMDTIMEOUT | MCI_CMDSENT | MCI_CMDRESPEND |
                                MCI_DATAEND | MCI_DATABLOCKEND;
    pl181->CLEAR = clear_mask;
    pl181->ARGUMENT = arg;
    pl181->COMMAND = (cmd & 0x3F) | flags | MCI_CMD_ENABLE;

    uint32_t wait = (flags & MCI_CMD_RESPONSE) ? MCI_CMDRESPEND : MCI_CMDSENT;
    uint32_t attempts = 2000;

    while (attempts--) {
        uint32_t s = pl181->STATUS;
        if (s & wait) {
            pl181->CLEAR = s & wait;
            return 0;
        }
        if (s & MCI_CMDTIMEOUT) {
            LOG_WARN(LOG_TAG, "CMD%u timeout", cmd);
            return -1;
        }
        if ((flags & MCI_CMD_RESPONSE) && (s & MCI_CMDCRCFAIL)) {
            LOG_WARN(LOG_TAG, "CMD%u CRC fail", cmd);
            return -1;
        }
        Pl181BusySpin(200);
    }

    LOG_WARN(LOG_TAG, "CMD%u poll timeout", cmd);
    return -1;
}

static int Pl181Setup(void)
{
    bool is_v2;

    pl181->POWER = MCI_POWER_UP | MCI_POWER_OPENDRAIN;
    pl181->CLOCK = (1U << 8) | 0x1D; /* enable, ~400 kHz */
    Pl181BusySpin(1000000);
    pl181->POWER = MCI_POWER_ON | MCI_POWER_OPENDRAIN;
    Pl181BusySpin(500000);

    /* CMD0: reset */
    if (Pl181IssueCmd(0, 0, 0) < 0)
        return -1;

    /* CMD8: interface condition; distinguishes v1 from v2 */
    if (Pl181IssueCmd(8, 0x000001AA, MCI_CMD_RESPONSE) == 0) {
        if ((pl181->RESPONSE[0] & 0xFFF) != 0x1AA) {
            LOG_ERROR(LOG_TAG, "voltage mismatch");
            return -1;
        }
        is_v2 = true;
    } else {
        is_v2 = false;
    }

    /* ACMD41: card power-up; loop until busy bit clears */
    uint32_t acmd41_arg = 0x00FF8000;
    if (is_v2)
        acmd41_arg |= (1U << 30); /* request SDHC */

    uint32_t ocr = 0;
    for (int retries = 1000; retries > 0; retries--) {
        if (Pl181IssueCmd(55, 0, MCI_CMD_RESPONSE) < 0)
            return -1;
        if (Pl181IssueCmd(41, acmd41_arg, MCI_CMD_RESPONSE) < 0)
            return -1;
        ocr = pl181->RESPONSE[0];
        if (ocr & (1U << 31))
            break;
        Pl181BusySpin(50000);
    }

    if (!(ocr & (1U << 31))) {
        LOG_ERROR(LOG_TAG, "card init timeout");
        return -1;
    }

    is_sdhc = (ocr & (1U << 30)) != 0;

    /* CMD2 - CID (required to advance card state machine) */
    if (Pl181IssueCmd(2, 0, MCI_CMD_RESPONSE | MCI_CMD_LONGRESP) < 0)
        return -1;

    /* CMD3 - get RCA */
    if (Pl181IssueCmd(3, 0, MCI_CMD_RESPONSE) < 0)
        return -1;
    uint32_t rca = pl181->RESPONSE[0] >> 16;

    /* CMD7 - select card → transfer state */
    if (Pl181IssueCmd(7, rca << 16, MCI_CMD_RESPONSE) < 0)
        return -1;

    LOG_INFO(LOG_TAG, "card ready, SDHC=%d", is_sdhc);

    /* switch to transfer-speed clock */
    pl181->CLOCK = (1U << 8) | 0x2; /* enable, ~25 MHz */
    Pl181BusySpin(100000);

    return 0;
}

static int WaitForIrq(uint32_t done_mask)
{
    for (uint32_t polls = 0; polls < XFER_TIMEOUT_POLLS; polls++) {
        if (pl181->STATUS & done_mask)
            return 0;
        WaitOn(g_event, POLL_MS);
    }
    return -1;
}

static int Pl181ReadBlk(uint32_t block_num, uint32_t *buf)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    /* arm interrupt mask before touching DATACTRL */
    pl181->MASK[0] = MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN;

    pl181->DATATIMER = 0xFFFFFFFF;
    pl181->DATALENGTH = MCI_BLOCK_SIZE;

    /* CMD17 - single block read (response required) */
    if (Pl181IssueCmd(17, addr, MCI_CMD_RESPONSE) < 0) {
        pl181->MASK[0] = 0;
        return SD_ERR_IO;
    }

    /* enable data path, card starts sending immediately */
    pl181->DATACTRL = MCI_DATACTRL_READ;

    /* sleep until MCI IRQ fires on DATAEND (or error) */
    if (WaitForIrq(MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN) < 0) {
        LOG_ERROR(LOG_TAG, "read timed out waiting for the controller");
        pl181->CLEAR = 0xFFFFFFFF;
        pl181->MASK[0] = 0;
        IrqRearm(block_dev_handle);
        pl181->DATACTRL = 0;
        return SD_ERR_IO;
    }

    uint32_t status = pl181->STATUS;

    /* check for transfer errors before draining */
    if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN)) {
        LOG_ERROR(LOG_TAG, "read error STATUS=0x%08x", status);
        pl181->CLEAR = 0xFFFFFFFF;
        pl181->MASK[0] = 0;
        IrqRearm(block_dev_handle);
        return SD_ERR_IO;
    }

    /* drain all 128 words - FIFO holds everything by the time we wake */
    for (uint32_t i = 0; i < MCI_BLOCK_WORDS; i++) {
        while (!(pl181->STATUS & MCI_RXDATAAVLBL))
            ;
        buf[i] = pl181->FIFO;
    }

    pl181->CLEAR = 0xFFFFFFFF;
    pl181->MASK[0] = 0;
    IrqRearm(block_dev_handle);
    pl181->DATACTRL = 0; /* disable data path before next transfer */
    return ZUZU_OK;
}

static int Pl181WriteBlk(uint32_t block_num, const uint32_t *buf)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    pl181->MASK[0] = MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_TXUNDERRUN;

    pl181->DATATIMER = 0xFFFFFFFF;
    pl181->DATALENGTH = MCI_BLOCK_SIZE;
    pl181->DATACTRL = MCI_DATACTRL_WRITE;

    /* CMD24 - single block write */
    if (Pl181IssueCmd(24, addr, MCI_CMD_RESPONSE) < 0) {
        pl181->MASK[0] = 0;
        return SD_ERR_IO;
    }

    /* push 128 words into FIFO, pacing on TXFIFOEMPTY */
    for (uint32_t i = 0; i < MCI_BLOCK_WORDS; i++) {
        while (!(pl181->STATUS & MCI_TXFIFOEMPTY))
            ;
        pl181->FIFO = buf[i];
    }

    /* sleep until card confirms block received */
    int waited = WaitForIrq(MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_TXUNDERRUN);

    uint32_t status = pl181->STATUS;

    pl181->CLEAR = 0xFFFFFFFF;
    pl181->MASK[0] = 0;
    IrqRearm(block_dev_handle);

    if (waited < 0 || (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_TXUNDERRUN))) {
        LOG_ERROR(LOG_TAG, "write error STATUS=0x%08x", status);
        return SD_ERR_IO;
    }

    return ZUZU_OK;
}

static void ReplyStatus(Err status)
{
    memcpy(MessageBuf(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void ServeGetBuf(void)
{
    /* Grants copy perms verbatim and need PERM_TXFR: send a dup. */
    SvcResult dup = HandleDuplicate(g_buf_mem, PERM_MAP | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK) {
        ReplyStatus((Err)dup.r0);
        return;
    }

    /* The card's capacity is not queried, so block_count is reported unknown. */
    SdReply rep = {
        .status = ZUZU_OK, .buf_size = SD_BUF_SIZE, .block_size = SD_BLOCK_SIZE, .block_count = 0};
    memcpy(MessageBuf(), &rep, sizeof(rep));
    Reply(sizeof(rep), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
}

static void ServeTransfer(const SdRequest *req)
{
    if (req->count == 0 || req->count > SD_BUF_SIZE / SD_BLOCK_SIZE) {
        ReplyStatus(ERR_BADARG);
        return;
    }

    int rc = ZUZU_OK;
    for (uint32_t i = 0; i < req->count && rc == ZUZU_OK; i++) {
        uint32_t *buf = shmem_buf + (i * MCI_BLOCK_WORDS);
        rc = (req->cmd == SD_CMD_READ) ? Pl181ReadBlk(req->lba + i, buf)
                                       : Pl181WriteBlk(req->lba + i, buf);
    }
    ReplyStatus((Err)rc);
}

static void HandleRequest(const PortWaitResult *r)
{
    if (r->granted >= 0)
        HandleClose(r->granted);

    uint32_t cmd = 0;
    if (r->xlen < sizeof(cmd)) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&cmd, MessageBuf(), sizeof(cmd));

    switch (cmd) {
    case SD_CMD_GET_BUF:
        ServeGetBuf();
        break;
    case SD_CMD_READ:
    case SD_CMD_WRITE: {
        SdRequest req;
        if (r->xlen < sizeof(req)) {
            ReplyStatus(ERR_BADARG);
            break;
        }
        memcpy(&req, MessageBuf(), sizeof(req));
        ServeTransfer(&req);
        break;
    }
    default:
        ReplyStatus(ERR_NOSYS);
        break;
    }
}

static Handle WaitForDevsvc(void)
{
    for (;;) {
        Handle h = LookupService("/svc/devsvc");
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

static int Pl181DrvSetup(void)
{
    Handle devsvc_port = WaitForDevsvc();

    /* request the block device capability */
    static const char *const block_compat[] = {"arm,pl180"}; /* note: pl180, the PL18x primecell */
    block_dev_handle = RequestDevice(devsvc_port, block_compat, 1, NULL);
    if (block_dev_handle < 0) {
        LOG_ERROR(LOG_TAG, "block device not present");
        return block_dev_handle;
    }

    g_event = CreateEvent();
    if (g_event < 0) {
        LOG_ERROR(LOG_TAG, "event create failed");
        return -1;
    }

    if (BindIrq(g_event, block_dev_handle, BIT_IRQ) < 0) {
        LOG_ERROR(LOG_TAG, "irq_bind failed");
        return -1;
    }

    void *mmio = MemMap(block_dev_handle, 0, PROT_RW);
    if (PtrIsErr(mmio)) {
        LOG_ERROR(LOG_TAG, "memmap failed");
        return -1;
    }
    pl181 = (Pl181Mmio *)mmio;

    uint32_t pid0 = pl181->PERIPHID[0] & 0xFF;
    uint32_t pid1 = pl181->PERIPHID[1] & 0xFF;
    if (!((pid0 == 0x80 || pid0 == 0x81) && pid1 == 0x11)) {
        LOG_ERROR(LOG_TAG, "unexpected peripheral ID %02x %02x", pid0, pid1);
        return -1;
    }
    LOG_INFO(LOG_TAG, "detected PL18%u", pid0 == 0x80 ? 0 : 1);

    if (Pl181Setup() < 0)
        return -1;

    g_buf_mem = CreateMem(SD_BUF_SIZE / 4096);
    if (g_buf_mem < 0) {
        LOG_ERROR(LOG_TAG, "shmem failed");
        return -1;
    }
    void *shm_addr = MemMap(g_buf_mem, 0, PROT_RW);
    if (PtrIsErr(shm_addr)) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return -1;
    }
    shmem_buf = (uint32_t *)shm_addr;

    port = CreatePort();
    if (port < 0 || Bind(EVENT_PORT, g_event, port, BIT_PORT) != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "port setup failed");
        return -1;
    }

    /* register after hardware is ready so clients don't find the port early */
    Err rc = RegisterService("/dev/mmc0", port);
    if (rc < 0) {
        LOG_ERROR(LOG_TAG, "service registration failed");
        return rc;
    }

    LOG_INFO(LOG_TAG, "serving /dev/mmc0");
    return 0;
}

int main(void)
{
    if (Pl181DrvSetup() < 0)
        return 1;

    for (;;) {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        /* A stray IRQ between transfers: the source is masked (MASK[0] == 0),
         * so just unmask the line again. */
        if (ev.status == ZUZU_OK && (ev.bits & MASK(BIT_IRQ)))
            IrqRearm(block_dev_handle);

        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }
    }
}
