#include "emmc2drv.h"
#include "zuzu/cap.h"
#include "zuzu/protocols/devm.h"
#include "zuzu/protocols/mmcdrv.h"
#include <stdbool.h>
#include <stdint.h>
#include <zuzu/log.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "pl181drv"

static Emmc2MMIO *emmc2;
static bool is_sdhc;

static Handle port = -1;
static Handle block_dev_handle = -1;
static Handle block_irq_ntfn = -1;
static Handle shmem_handle = -1;
static uint32_t *shmem_buf = NULL;
static Handle pending_reply_h = -1;
static int current_op = 0; /* 0 = IDLE, 1 = READ, 2 = WRITE */

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
    while ((emmc2->present_state & SDHCI_CMD_INHIBIT) && attempts--)
    {
        BusyDelay(10);
    }
    if (attempts == 0)
    {
        LOG_WARN(LOG_TAG, "CMD%u timeout waiting for CMD_INHIBIT to clear", cmd);
        return -1;
    }

    /* If it's a data transfer command, wait for data lines to be free too */
    if (flags & SDHCI_CMD_DATA_EN)
    {
        attempts = 10000;
        while ((emmc2->present_state & SDHCI_DATA_INHIBIT) && attempts--)
        {
            BusyDelay(10);
        }
        if (attempts == 0)
        {
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
    while (attempts--)
    {
        uint32_t s = emmc2->int_status;

        /* Check for global error flag first (Timeout, CRC, etc. are bits 16-31) */
        if (s & SDHCI_INT_ERROR)
        {
            LOG_WARN(LOG_TAG, "CMD%u error, int_status=0x%08x", cmd, s);
            emmc2->int_status = s; /* Clear the error bits */

            /* SDHCI spec recommends resetting the CMD line on error */
            emmc2->clk_ctrl_reset |= SDHCI_RESET_CMD;
            while (emmc2->clk_ctrl_reset & SDHCI_RESET_CMD)
                ;

            return -1;
        }

        /* Check for successful command completion */
        if (s & SDHCI_INT_CMD_COMPLETE)
        {
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
    while (emmc2->clk_ctrl_reset & SDHCI_RESET_ALL)
    {
        BusyDelay(10);
    }

    /* Power On to 3.3V */
    emmc2->host_ctrl_1 = SDHCI_POWER_330V | SDHCI_POWER_ON;
    ZuzuSleep(10);

    /* 3. Enable internal clock & set divider for ~400kHz (identification mode) */
    /* SDHCI 3.0 uses bits 8-15 for the base clock divider */
    emmc2->clk_ctrl_reset = SDHCI_CLK_INT_EN | (0x40 << 8);
    while (!(emmc2->clk_ctrl_reset & SDHCI_CLK_STABLE))
    {
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
    if (Emmc2SendCmd(8, 0x000001AA, cmd8_flags) == 0)
    {
        /* SDHCI natively strips the 8-bit command/CRC, so the payload starts at bit 0 */
        if ((emmc2->response[0] & 0xFFF) != 0x1AA)
        {
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
    for (int retries = 1000; retries > 0; retries--)
    {
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
        ZuzuSleep(10);
    }

    if (!(ocr & (1U << 31)))
    {
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
    while (!(emmc2->clk_ctrl_reset & SDHCI_CLK_STABLE))
    {
        BusyDelay(10);
    }
    emmc2->clk_ctrl_reset |= SDHCI_CLK_SD_EN;

    return 0;
}

static void StartRead(uint32_t block_num, Handle reply_h)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    pending_reply_h = reply_h;
    current_op = 1;

    emmc2->block_size_count = (1U << 16) | 512U;
    emmc2->int_status = 0xFFFFFFFF;

    if (Emmc2SendCmd(17, addr,
                     SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK |
                         SDHCI_CMD_DATA_EN | SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN) < 0)
    {
        ZuzuMsgReply(reply_h, (uint32_t)SD_ERR_IO, 0, 0);
        current_op = 0;
    }
}

static void StartWrite(uint32_t block_num, Handle reply_h)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    pending_reply_h = reply_h;
    current_op = 2;

    emmc2->block_size_count = (1U << 16) | 512U;
    emmc2->int_status = 0xFFFFFFFF;

    if (Emmc2SendCmd(24, addr,
                     SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_IDX_CHECK |
                         SDHCI_CMD_DATA_EN | SDHCI_TRNS_BLK_CNT_EN) < 0)
    {
        ZuzuMsgReply(reply_h, (uint32_t)SD_ERR_IO, 0, 0);
        current_op = 0;
    }
}

static void HandleHardwareIrq(void)
{
    uint32_t status = emmc2->int_status;
    int rc = ZUZU_OK;

    if (status & SDHCI_INT_ERROR)
    {
        LOG_ERROR(LOG_TAG, "transfer error STATUS=0x%08x", status);
        emmc2->int_status = status;
        emmc2->clk_ctrl_reset |= SDHCI_RESET_DATA;
        while (emmc2->clk_ctrl_reset & SDHCI_RESET_DATA)
            ;
        rc = SD_ERR_IO;
    }
    else if (current_op == 1 && (status & SDHCI_INT_BUF_RD_READY))
    {
        emmc2->int_status = SDHCI_INT_BUF_RD_READY;
        for (size_t i = 0; i < MCI_BLOCK_WORDS; i++)
        {
            shmem_buf[i] = emmc2->data_port;
        }
        while (!(emmc2->int_status & SDHCI_INT_XFER_COMPLETE))
            ;
        emmc2->int_status = SDHCI_INT_XFER_COMPLETE;
    }
    else if (current_op == 2 && (status & SDHCI_INT_BUF_WR_READY))
    {
        emmc2->int_status = SDHCI_INT_BUF_WR_READY;
        for (size_t i = 0; i < MCI_BLOCK_WORDS; i++)
        {
            emmc2->data_port = shmem_buf[i]; /* Fixed: push to hardware */
        }
        while (!(emmc2->int_status & SDHCI_INT_XFER_COMPLETE))
            ;
        emmc2->int_status = SDHCI_INT_XFER_COMPLETE;
    }
    else
    {
        /* Spurious or XFER_COMPLETE from a previous step */
        emmc2->int_status = status;
        ZuzuIrqDone(block_dev_handle);
        return;
    }

    ZuzuIrqDone(block_dev_handle);

    if (pending_reply_h != -1)
    {
        ZuzuMsgReply(pending_reply_h, (uint32_t)rc, 0, 0);
        pending_reply_h = -1;
    }
    current_op = 0;
}

static void ServeClient(Handle reply_h, uint32_t sender, uint32_t cmd, uint32_t arg)
{
    switch (cmd)
    {
    case SD_CMD_GET_BUF:
    {
        int32_t granted = ZuzuGrant(shmem_handle, (int32_t)sender, 0);
        if (granted < 0)
            ZuzuMsgReply(reply_h, (uint32_t)granted, 0, 0);
        else
            ZuzuMsgReply(reply_h, ZUZU_OK, (uint32_t)granted, 0);
        break;
    }
    case SD_CMD_READ:
        StartRead(arg, reply_h);
        break;
    case SD_CMD_WRITE:
        StartWrite(arg, reply_h);
        break;
    default:
        ZuzuMsgReply(reply_h, (uint32_t)ERR_NOSYS, 0, 0);
        break;
    }
}

static int Emmc2ServiceInit(void)
{
    int32_t devmgr_port = LookupService("/svc/devmgr");
    if (devmgr_port < 0)
        return -1;

    static const char *const block_compat[] = {"brcm,bcm2711-emmc2"};
    block_dev_handle = DevmRequestDevice(devmgr_port, block_compat, 1, NULL);
    if (block_dev_handle < 0)
        return block_dev_handle;

    block_irq_ntfn = ZuzuNtfnCreate();
    if (block_irq_ntfn < 0 || ZuzuIrqBind(block_dev_handle, block_irq_ntfn) < 0)
        return -1;

    emmc2 = (Emmc2MMIO *)ZuzuMemMap(block_dev_handle, 0, PROT_RW, 0);
    if ((intptr_t)emmc2 <= 0)
        return -1;

    if (Emmc2HwInit() < 0)
        return -1;

    Handle shm_h = ZuzuShmemCreate(4096);
    void *shm_addr = ZuzuMemMap(shm_h, 0, PROT_RW, 0);
    if (ZuzuPtrIsErr(shm_addr))
        return -1;

    shmem_handle = shm_h;
    shmem_buf = (uint32_t *)shm_addr;

    port = ZuzuPortCreate();
    return RegisterService("/dev/mmc0", port);
}

int main(void)
{
    if (Emmc2ServiceInit() < 0)
        return 1;

    Handle wait_handles[2] = {port, block_irq_ntfn};

    while (1)
    {
        /* Per-iteration, and stamped NO_MATCH rather than merely zeroed:
         * index 0 is a live handle here, so a zeroed result would dispatch
         * as a port request carrying the previous iteration's source and
         * LBA. waitany can return success without writing this. */
        WaitanyResult res;
        res.matched_index = (Handle)WAITANY_NO_MATCH;

        Err err = ZuzuWaitany(wait_handles, 2, TIMEOUT_INFINITE, &res);
        if (err < 0 || res.matched_index == (Handle)WAITANY_NO_MATCH)
        {
            continue;
        }

        if (res.matched_index == 0)
        {
            if (current_op != 0 && (res.w2 == SD_CMD_READ || res.w2 == SD_CMD_WRITE))
            {
                ZuzuMsgReply((Handle)res.source, (uint32_t)ERR_BUSY, 0, 0);
            }
            else
            {
                ServeClient((Handle)res.source, res.w1, res.w2, res.w3);
            }
        }
        else if (res.matched_index == 1)
        {
            if (res.w1 > 0)
            {
                HandleHardwareIrq();
            }
        }
    }
}