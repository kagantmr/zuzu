#include <zuzu/zuzu.h>
#include <zuzu/service.h>
#include <zuzu/log.h>
#include <stdbool.h>
#include <stdint.h>
#include "pl181drv.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/sd_protocol.h"

#define LOG_TAG "pl181drv"

static pl181_t *pl181;
static bool is_sdhc;

static int32_t port = -1;
static int32_t block_dev_handle = -1;
static int32_t block_irq_ntfn = -1;
static int32_t shmem_handle = -1;
static uint32_t *shmem_buf = NULL;

static void pl181_delay(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++)
        __asm__ volatile("nop");
}

static int pl181_send_cmd(uint32_t cmd, uint32_t arg, uint32_t flags)
{
    const uint32_t clear_mask = MCI_CMDCRCFAIL | MCI_CMDTIMEOUT | MCI_CMDSENT |
                                MCI_CMDRESPEND | MCI_DATAEND | MCI_DATABLOCKEND;
    pl181->CLEAR = clear_mask;
    pl181->ARGUMENT = arg;
    pl181->COMMAND = (cmd & 0x3F) | flags | MCI_CMD_ENABLE;

    uint32_t wait = (flags & MCI_CMD_RESPONSE) ? MCI_CMDRESPEND : MCI_CMDSENT;
    uint32_t attempts = 2000;

    while (attempts--)
    {
        uint32_t s = pl181->STATUS;
        if (s & wait)
        {
            pl181->CLEAR = s & wait;
            return 0;
        }
        if (s & MCI_CMDTIMEOUT)
        {
            LOG_WARN(LOG_TAG, "CMD%u timeout", cmd);
            return -1;
        }
        if ((flags & MCI_CMD_RESPONSE) && (s & MCI_CMDCRCFAIL))
        {
            LOG_WARN(LOG_TAG, "CMD%u CRC fail", cmd);
            return -1;
        }
        pl181_delay(200);
    }

    LOG_WARN(LOG_TAG, "CMD%u poll timeout", cmd);
    return -1;
}

static int pl181_setup(void)
{
    bool is_v2;

    pl181->POWER = MCI_POWER_UP | MCI_POWER_OPENDRAIN;
    pl181->CLOCK = (1u << 8) | 0x1D; /* enable, ~400 kHz */
    pl181_delay(1000000);
    pl181->POWER = MCI_POWER_ON | MCI_POWER_OPENDRAIN;
    pl181_delay(500000);

    /* CMD0: reset */
    if (pl181_send_cmd(0, 0, 0) < 0)
        return -1;

    /* CMD8: interface condition; distinguishes v1 from v2 */
    if (pl181_send_cmd(8, 0x000001AA, MCI_CMD_RESPONSE) == 0)
    {
        if ((pl181->RESPONSE[0] & 0xFFF) != 0x1AA)
        {
            LOG_ERROR(LOG_TAG, "voltage mismatch");
            return -1;
        }
        is_v2 = true;
    }
    else
    {
        is_v2 = false;
    }

    /* ACMD41: card power-up; loop until busy bit clears */
    uint32_t acmd41_arg = 0x00FF8000;
    if (is_v2)
        acmd41_arg |= (1u << 30); /* request SDHC */

    uint32_t ocr = 0;
    for (int retries = 1000; retries > 0; retries--)
    {
        if (pl181_send_cmd(55, 0, MCI_CMD_RESPONSE) < 0)
            return -1;
        if (pl181_send_cmd(41, acmd41_arg, MCI_CMD_RESPONSE) < 0)
            return -1;
        ocr = pl181->RESPONSE[0];
        if (ocr & (1u << 31))
            break;
        pl181_delay(50000);
    }

    if (!(ocr & (1u << 31)))
    {
        LOG_ERROR(LOG_TAG, "card init timeout");
        return -1;
    }

    is_sdhc = (ocr & (1u << 30)) != 0;

    /* CMD2 - CID (required to advance card state machine) */
    if (pl181_send_cmd(2, 0, MCI_CMD_RESPONSE | MCI_CMD_LONGRESP) < 0)
        return -1;

    /* CMD3 - get RCA */
    if (pl181_send_cmd(3, 0, MCI_CMD_RESPONSE) < 0)
        return -1;
    uint32_t rca = pl181->RESPONSE[0] >> 16;

    /* CMD7 - select card → transfer state */
    if (pl181_send_cmd(7, rca << 16, MCI_CMD_RESPONSE) < 0)
        return -1;

    LOG_INFO(LOG_TAG, "card ready, SDHC=%d", is_sdhc);

    /* switch to transfer-speed clock */
    pl181->CLOCK = (1u << 8) | 0x2; /* enable, ~25 MHz */
    pl181_delay(100000);

    return 0;
}

static int wait_for_irq(void)
{
    int32_t bits = _ntfn_wait((uint32_t)block_irq_ntfn, TIMEOUT_INFINITE);
    return (bits < 0) ? -1 : 0;
}

static int pl181_read_block(uint32_t block_num)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    /* arm interrupt mask before touching DATACTRL */
    pl181->MASK[0] = MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN;

    pl181->DATATIMER = 0xFFFFFFFF;
    pl181->DATALENGTH = MCI_BLOCK_SIZE;

    /* CMD17 - single block read (response required) */
    if (pl181_send_cmd(17, addr, MCI_CMD_RESPONSE) < 0)
    {
        pl181->MASK[0] = 0;
        return SD_ERR_IO;
    }

    /* enable data path, card starts sending immediately */
    pl181->DATACTRL = MCI_DATACTRL_READ;

    /* sleep until MCI IRQ fires on DATAEND (or error) */
    wait_for_irq();

    uint32_t status = pl181->STATUS;

    /* check for transfer errors before draining */
    if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN))
    {
        LOG_ERROR(LOG_TAG, "read error STATUS=0x%08x", status);
        pl181->CLEAR = 0xFFFFFFFF;
        pl181->MASK[0] = 0;
        _irq_done((uint32_t)block_dev_handle);
        return SD_ERR_IO;
    }

    /* drain all 128 words - FIFO holds everything by the time we wake */
    for (uint32_t i = 0; i < MCI_BLOCK_WORDS; i++)
    {
        while (!(pl181->STATUS & MCI_RXDATAAVLBL))
            ;
        shmem_buf[i] = pl181->FIFO;
    }

    pl181->CLEAR = 0xFFFFFFFF;
    pl181->MASK[0] = 0;
    _irq_done((uint32_t)block_dev_handle);
    pl181->DATACTRL = 0;   /* disable data path before next transfer */
    return ZUZU_OK;
}

static int pl181_write_block(uint32_t block_num)
{
    uint32_t addr = is_sdhc ? block_num : block_num * MCI_BLOCK_SIZE;

    pl181->MASK[0] = MCI_DATAEND | MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_TXUNDERRUN;

    pl181->DATATIMER = 0xFFFFFFFF;
    pl181->DATALENGTH = MCI_BLOCK_SIZE;
    pl181->DATACTRL = MCI_DATACTRL_WRITE;

    /* CMD24 - single block write */
    if (pl181_send_cmd(24, addr, MCI_CMD_RESPONSE) < 0)
    {
        pl181->MASK[0] = 0;
        return SD_ERR_IO;
    }

    /* push 128 words into FIFO, pacing on TXFIFOEMPTY */
    for (uint32_t i = 0; i < MCI_BLOCK_WORDS; i++)
    {
        while (!(pl181->STATUS & MCI_TXFIFOEMPTY))
            ;
        pl181->FIFO = shmem_buf[i];
    }

    /* sleep until card confirms block received */
    wait_for_irq();

    uint32_t status = pl181->STATUS;

    pl181->CLEAR = 0xFFFFFFFF;
    pl181->MASK[0] = 0;
    _irq_done((uint32_t)block_dev_handle);

    if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_TXUNDERRUN))
    {
        LOG_ERROR(LOG_TAG, "write error STATUS=0x%08x", status);
        return SD_ERR_IO;
    }

    return ZUZU_OK;
}

static void handle_client(msg_t msg)
{
    uint32_t reply_h = (uint32_t)msg.r0;
    uint32_t sender = msg.r1;
    uint32_t cmd = msg.r2;
    uint32_t arg = msg.r3;

    switch (cmd)
    {

    case SD_CMD_GET_BUF:
    {
        int32_t granted = _grant(shmem_handle, (int32_t)sender);
        if (granted < 0)
            _reply(reply_h, (uint32_t)granted, 0, 0);
        else
            _reply(reply_h, ZUZU_OK, (uint32_t)granted, 0);
        break;
    }

    case SD_CMD_READ:
    {
        int rc = pl181_read_block(arg);
        _reply(reply_h, (uint32_t)rc, 0, 0);
        break;
    }

    case SD_CMD_WRITE:
    {
        int rc = pl181_write_block(arg);
        _reply(reply_h, (uint32_t)rc, 0, 0);
        break;
    }

    default:
        _reply(reply_h, (uint32_t)ERR_NOSYS, 0, 0);
        break;
    }
}

static int pl181drv_setup(void)
{
    int32_t devmgr_port = lookup_service("devm");
    if (devmgr_port < 0)
    {
        LOG_ERROR(LOG_TAG, "devmgr lookup failed");
        return -1;
    }

    /* request the block device capability */
    msg_t r = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_BLOCK, 0);
    if ((int32_t)r.r1 != 0)
    {
        LOG_ERROR(LOG_TAG, "block device request failed");
        return -1;
    }
    block_dev_handle = (int32_t)r.r2;

    block_irq_ntfn = _ntfn_create();
    if (block_irq_ntfn < 0)
    {
        LOG_ERROR(LOG_TAG, "ntfn_create failed");
        return -1;
    }

    if (_irq_claim((uint32_t)block_dev_handle) < 0)
    {
        LOG_ERROR(LOG_TAG, "irq_claim failed");
        return -1;
    }
    if (_irq_bind((uint32_t)block_dev_handle, (uint32_t)block_irq_ntfn) < 0)
    {
        LOG_ERROR(LOG_TAG, "irq_bind failed");
        return -1;
    }

    pl181 = (pl181_t *)_memmap((uint32_t)block_dev_handle, 0, VM_PROT_RW, 0);
    if ((intptr_t)pl181 <= 0)
    {
        LOG_ERROR(LOG_TAG, "mapdev failed");
        return -1;
    }

    uint32_t pid0 = pl181->PERIPHID[0] & 0xFF;
    uint32_t pid1 = pl181->PERIPHID[1] & 0xFF;
    if (!((pid0 == 0x80 || pid0 == 0x81) && pid1 == 0x11))
    {
        LOG_ERROR(LOG_TAG, "unexpected peripheral ID %02x %02x", pid0, pid1);
        return -1;
    }
    LOG_INFO(LOG_TAG, "detected PL18%u", pid0 == 0x80 ? 0 : 1);

    if (pl181_setup() < 0)
        return -1;

    handle_t shm_h = _shm_create(4096);
    if (shm_h < 0)
    {
        LOG_ERROR(LOG_TAG, "shmem failed");
        return -1;
    }
    void *shm_addr = _memmap(shm_h, 0, VM_PROT_RW, 0);
    if (_ptr_is_err(shm_addr))
    {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return -1;
    }
    shmem_handle = shm_h;
    shmem_buf = (uint32_t *)shm_addr;

    /* register after hardware is ready so clients don't find the port early */
    port = register_service("pl181drv");
    if (port < 0)
    {
        LOG_ERROR(LOG_TAG, "service registration failed");
        return -1;
    }

    return 0;
}

int main(void)
{
    if (pl181drv_setup() < 0)
        return 1;

    while (1)
    {
        /* drain any stray IRQ that arrived between transfers */
        int32_t bits = _ntfn_wait((uint32_t)block_irq_ntfn, TIMEOUT_POLL);
        if (bits > 0)
            _irq_done((uint32_t)block_dev_handle);

        msg_t msg = _recv(port);
        if ((int32_t)msg.r0 > 0)
            handle_client(msg);
    }
}
