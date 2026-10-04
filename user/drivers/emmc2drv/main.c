#include "dev/protocols/devm.h"
#include "dev/protocols/mmcdrv.h"
#include "emmc2drv.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "emmc2drv"
#define LOG_INFO(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) UserspaceDebugLog(tag ": " fmt, ##__VA_ARGS__)

#define BIT_IRQ 0
#define BIT_PORT 1
#define MASK(bit) (1U << (bit))
#define POLL_MS 10 /* bits are hints: int_status is re-checked at least this often */
#define XFER_TIMEOUT_POLLS 500

#define OP_READ 1
#define OP_WRITE 2

static Emmc2MMIO *emmc2;
static bool is_sdhc;

static Handle port = -1;
static Handle block_dev_handle = -1;
static Handle g_event = -1;
static Handle g_buf_mem = -1;
static uint32_t *shmem_buf = NULL;
static int current_op = 0;

static void BusyDelay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++)
        __asm__ volatile("nop");
}

static int Emmc2SendCmd(uint32_t cmd, uint32_t arg, uint32_t flags)
{
    /* 1. Wait for the command line to be free */
    uint32_t attempts = 10000;
    while ((emmc2->present_state & SDHCI_CMD_INHIBIT) && attempts--) {
        BusyDelay(10);
    }
    if (attempts == 0) {
        LOG_WARN(LOG_TAG, "CMD%u timeout waiting for CMD_INHIBIT to clear", cmd);
        return -1;
    }

    /* If it's a data transfer command, wait for data lines to be free too */
    if (flags & SDHCI_CMD_DATA_EN) {
        attempts = 10000;
        while ((emmc2->present_state & SDHCI_DATA_INHIBIT) && attempts--) {
            BusyDelay(10);
        }
        if (attempts == 0) {
            LOG_WARN(LOG_TAG, "CMD%u timeout waiting for DATA_INHIBIT to clear", cmd);
            return -1;
        }
    }

    /* 2. Clear any pending interrupt statuses (Write-1-to-Clear) */
    emmc2->int_status = 0xFFFFFFFF;

    /* 3. Load the argument */
    emmc2->argument = arg;

    /* 4. Issue the command (writing to cmd_xfer_mode triggers the hardware) */
    emmc2->cmd_xfer_mode = SDHCI_CMD_INDEX(cmd) | flags;

    /* 5. Poll for completion or error */
    attempts = 10000;
    while (attempts--) {
        uint32_t s = emmc2->int_status;

        /* Check for global error flag first (Timeout, CRC, etc. are bits 16-31) */
        if (s & SDHCI_INT_ERROR) {
            LOG_WARN(LOG_TAG, "CMD%u error, int_status=0x%08x", cmd, s);
            emmc2->int_status = s; /* Clear the error bits */

            /* SDHCI spec recommends resetting the CMD line on error */
            emmc2->clk_ctrl_reset |= SDHCI_RESET_CMD;
            while (emmc2->clk_ctrl_reset & SDHCI_RESET_CMD)
                ;

            return -1;
        }

        /* Check for successful command completion */
        if (s & SDHCI_INT_CMD_COMPLETE) {
            emmc2->int_status = SDHCI_INT_CMD_COMPLETE; /* Clear the complete flag */
            return 0;
        }

        BusyDelay(200);
    }

    LOG_WARN(LOG_TAG, "CMD%u poll timeout, int_status=0x%08x", cmd, emmc2->int_status);
    return -1;
}

static int Emmc2HwInit(void)
{
    bool is_v2 = false;

    /* software reset */
    emmc2->clk_ctrl_reset = SDHCI_RESET_ALL;
    while (emmc2->clk_ctrl_reset & SDHCI_RESET_ALL) {
        BusyDelay(10);
    }

    /* Power On to 3.3V */
    emmc2->host_ctrl_1 = SDHCI_POWER_330V | SDHCI_POWER_ON;
    Sleep(10);

    /* 3. Enable internal clock & set divider for ~400kHz (identification mode) */
    /* SDHCI 3.0 uses bits 8-15 for the base clock divider */
    emmc2->clk_ctrl_reset = SDHCI_CLK_INT_EN | (0x40 << 8);
    while (!(emmc2->clk_ctrl_reset & SDHCI_CLK_STABLE)) {
        BusyDelay(10);
    }
    emmc2->clk_ctrl_reset |= SDHCI_CLK_SD_EN;

    /* 4. Enable all interrupts to pass through to the int_status register and global IRQ */
    emmc2->int_enable = 0xFFFFFFFF;
    emmc2->int_signal = 0xFFFFFFFF;

    /* CMD0: reset */
    if (Emmc2SendCmd(0, 0, SDHCI_CMD_RESP_NONE) < 0)
        return -1;

    /* CMD8: interface condition */
    uint32_t cmd8_flags = SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK;
    if (Emmc2SendCmd(8, 0x000001AA, cmd8_flags) == 0) {
        /* SDHCI natively strips the 8-bit command/CRC, so the payload starts at bit 0 */
        if ((emmc2->response[0] & 0xFFF) != 0x1AA) {
            LOG_ERROR(LOG_TAG, "voltage mismatch");
            return -1;
        }
        is_v2 = true;
    }

    /* ACMD41: card power-up */
    uint32_t acmd41_arg = 0x00FF8000;
    if (is_v2)
        acmd41_arg |= (1U << 30); /* request SDHC */

    uint32_t ocr = 0;
    for (int retries = 1000; retries > 0; retries--) {
        /* CMD55 prefixes ACMD41 */
        if (Emmc2SendCmd(55, 0, SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK) < 0)
            return -1;

        /* ACMD41 does not include a CRC in its response */
        if (Emmc2SendCmd(41, acmd41_arg, SDHCI_CMD_RESP_48) < 0)
            return -1;

        ocr = emmc2->response[0];
        if (ocr & (1U << 31))
            break;

        /* Yield thread while waiting for physical card power up */
        Sleep(10);
    }

    if (!(ocr & (1U << 31))) {
        LOG_ERROR(LOG_TAG, "card init timeout");
        return -1;
    }

    is_sdhc = (ocr & (1U << 30)) != 0;

    /* CMD2: CID (136-bit response) */
    if (Emmc2SendCmd(2, 0, SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK) < 0)
        return -1;

    /* CMD3: get RCA */
    if (Emmc2SendCmd(3, 0, SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK) < 0)
        return -1;

    uint32_t rca = emmc2->response[0] >> 16;

    /* CMD7: select card (Response includes a busy signal) */
    if (Emmc2SendCmd(7, rca << 16,
                     SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK) < 0)
        return -1;

    LOG_INFO(LOG_TAG, "card ready, SDHC=%d", is_sdhc);

    /* Switch to transfer-speed clock (e.g. 25MHz) */
    emmc2->clk_ctrl_reset &= ~SDHCI_CLK_SD_EN; /* SD clock must be disabled to change divider */
    emmc2->clk_ctrl_reset = SDHCI_CLK_INT_EN | (0x02 << 8); /* Lower divider for higher speed */
    while (!(emmc2->clk_ctrl_reset & SDHCI_CLK_STABLE)) {
        BusyDelay(10);
    }
    emmc2->clk_ctrl_reset |= SDHCI_CLK_SD_EN;

    return 0;
}

/* Synchronous transfer: after the command is issued, sleep on the event until
 * the controller raises BUF_RD/WR_READY (or an error), move the data, and
 * return. The IRQ line is masked by the kernel on every interrupt, so the
 * source is cleared first, then the line re-armed, then int_status re-read. */
static int Emmc2WaitTransfer(uint32_t *buf)
{
    bool irq = true; /* command interrupts may already have masked the line */

    for (uint32_t polls = 0; polls < XFER_TIMEOUT_POLLS; polls++) {
        uint32_t status = emmc2->int_status;

        if (status & SDHCI_INT_ERROR) {
            LOG_ERROR(LOG_TAG, "transfer error STATUS=0x%08x", status);
            emmc2->int_status = status;
            emmc2->clk_ctrl_reset |= SDHCI_RESET_DATA;
            while (emmc2->clk_ctrl_reset & SDHCI_RESET_DATA)
                ;
            IrqRearm(block_dev_handle);
            return SD_ERR_IO;
        }
        if (current_op == OP_READ && (status & SDHCI_INT_BUF_RD_READY)) {
            emmc2->int_status = SDHCI_INT_BUF_RD_READY;
            for (size_t i = 0; i < MCI_BLOCK_WORDS; i++) {
                buf[i] = emmc2->data_port;
            }
            while (!(emmc2->int_status & SDHCI_INT_XFER_COMPLETE))
                ;
            emmc2->int_status = SDHCI_INT_XFER_COMPLETE;
            IrqRearm(block_dev_handle);
            return ZUZU_OK;
        }
        if (current_op == OP_WRITE && (status & SDHCI_INT_BUF_WR_READY)) {
            emmc2->int_status = SDHCI_INT_BUF_WR_READY;
            for (size_t i = 0; i < MCI_BLOCK_WORDS; i++) {
                emmc2->data_port = buf[i];
            }
            while (!(emmc2->int_status & SDHCI_INT_XFER_COMPLETE))
                ;
            emmc2->int_status = SDHCI_INT_XFER_COMPLETE;
            IrqRearm(block_dev_handle);
            return ZUZU_OK;
        }

        /* Spurious, or XFER_COMPLETE from a previous step */
        if (status)
            emmc2->int_status = status;
        if (irq) {
            IrqRearm(block_dev_handle);
            irq = false;
        }

        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        irq = ev.status == ZUZU_OK && (ev.bits & MASK(BIT_IRQ));
    }

    LOG_ERROR(LOG_TAG, "transfer timed out");
    return SD_ERR_IO;
}

static int Emmc2Transfer(int op, uint32_t block_num, uint32_t *buf)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    current_op = op;
    emmc2->block_size_count = (1U << 16) | 512U;
    emmc2->int_status = 0xFFFFFFFF;

    int rc;
    if (op == OP_READ) {
        rc = Emmc2SendCmd(17, addr,
                          SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK |
                              SDHCI_CMD_DATA_EN | SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);
    } else {
        rc = Emmc2SendCmd(24, addr,
                          SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK |
                              SDHCI_CMD_DATA_EN | SDHCI_TRNS_BLK_CNT_EN);
    }

    rc = (rc < 0) ? SD_ERR_IO : Emmc2WaitTransfer(buf);
    current_op = 0;
    return rc;
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

    int op = (req->cmd == SD_CMD_READ) ? OP_READ : OP_WRITE;
    int rc = ZUZU_OK;
    for (uint32_t i = 0; i < req->count && rc == ZUZU_OK; i++)
        rc = Emmc2Transfer(op, req->lba + i, shmem_buf + (i * MCI_BLOCK_WORDS));
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

static int Emmc2ServiceInit(void)
{
    Handle devsvc_port = WaitForDevsvc();

    static const char *const block_compat[] = {"brcm,bcm2711-emmc2"};
    block_dev_handle = RequestDevice(devsvc_port, block_compat, 1, NULL);
    if (block_dev_handle < 0)
        return block_dev_handle;

    g_event = CreateEvent();
    if (g_event < 0 || BindIrq(g_event, block_dev_handle, BIT_IRQ) != ZUZU_OK)
        return -1;

    void *mmio = MemMap(block_dev_handle, 0, PROT_RW);
    if (PtrIsErr(mmio))
        return -1;
    emmc2 = (Emmc2MMIO *)mmio;

    if (Emmc2HwInit() < 0)
        return -1;

    g_buf_mem = CreateMem(SD_BUF_SIZE / 4096);
    if (g_buf_mem < 0)
        return -1;
    void *shm_addr = MemMap(g_buf_mem, 0, PROT_RW);
    if (PtrIsErr(shm_addr))
        return -1;
    shmem_buf = (uint32_t *)shm_addr;

    port = CreatePort();
    if (port < 0 || Bind(EVENT_PORT, g_event, port, BIT_PORT) != ZUZU_OK)
        return -1;
    return RegisterService("/dev/mmc0", port);
}

int main(void)
{
    if (Emmc2ServiceInit() < 0)
        return 1;

    for (;;) {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        /* A stray IRQ between transfers: clear the source, then re-arm the line. */
        if (ev.status == ZUZU_OK && (ev.bits & MASK(BIT_IRQ))) {
            emmc2->int_status = emmc2->int_status;
            IrqRearm(block_dev_handle);
        }

        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }
    }
}
