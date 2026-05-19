#include <zuzu/zuzu.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "pl181drv.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/sd_protocol.h"
#include <malloc.h>
#include <zuzu/user_layout.h>

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
            printf("pl181drv: CMD%u timeout\n", cmd);
            return -1;
        }
        if ((flags & MCI_CMD_RESPONSE) && (s & MCI_CMDCRCFAIL))
        {
            printf("pl181drv: CMD%u CRC fail\n", cmd);
            return -1;
        }
        pl181_delay(200);
    }

    printf("pl181drv: CMD%u poll timeout\n", cmd);
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
            printf("pl181drv: voltage mismatch\n");
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
        printf("pl181drv: card init timeout\n");
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

    printf("pl181drv: card ready, SDHC=%d\n", is_sdhc);

    /* switch to transfer-speed clock */
    pl181->CLOCK = (1u << 8) | 0x2; /* enable, ~25 MHz */
    pl181_delay(100000);

    return 0;
}

/*
 * Wait for the MCI IRQ to arrive on our notification object.
 * Returns 0 on clean IRQ delivery.
 */
static int wait_for_irq(void)
{
    int32_t bits = _ntfn_wait((uint32_t)block_irq_ntfn);
    return (bits < 0) ? -1 : 0;
}

/* ... rest of functions (pl181_read_block, pl181_write_block, worker, handlers) ... */

/* Block read: arm transfer, sleep until IRQ, drain FIFO, report status.
 * Result ends up in shmem_buf.
 */
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
        return -1;
    }

    /* enable data path, card starts sending immediately */
    pl181->DATACTRL = MCI_DATACTRL_READ;

    /* sleep until MCI IRQ fires on DATAEND (or error) */
    wait_for_irq();

    uint32_t status = pl181->STATUS;

    /* check for transfer errors before draining */
    if (status & (MCI_DATACRCFAIL | MCI_DATATIMEOUT | MCI_RXOVERRUN))
    {
        printf("pl181drv: read error STATUS=0x%08x\n", status);
        pl181->CLEAR = 0xFFFFFFFF;
        pl181->MASK[0] = 0;
        _irq_done((uint32_t)block_dev_handle);
        return -1;
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
    return 0;
}

/* Block write: arm data path, fill FIFO, sleep until IRQ, check status.
 * Data is read from shmem_buf.
 */
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
        return -1;
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
        printf("pl181drv: write error STATUS=0x%08x\n", status);
        return -1;
    }

    return 0;
}

/* job queue + worker thread to offload blocking block transfers. */

#define PL181DRV_JOB_QUEUE_SZ 16
typedef struct {
    uint32_t cmd;
    uint32_t arg;
    uint32_t reply_h;
    uint32_t sender;
} pl181drv_job_t;

static pl181drv_job_t pl181drv_jobs[PL181DRV_JOB_QUEUE_SZ];
static unsigned pl181drv_job_head = 0;
static unsigned pl181drv_job_tail = 0;
static int32_t pl181drv_worker_ntfn = -1;

/* forward declarations for block transfer helpers */
static int pl181_read_block(uint32_t block_num);
static int pl181_write_block(uint32_t block_num);

static void pl181drv_worker(void *arg)
{
    (void)arg;
    while (1) {
        _ntfn_wait((uint32_t)pl181drv_worker_ntfn);
        while (pl181drv_job_head != pl181drv_job_tail) {
            pl181drv_job_t job = pl181drv_jobs[pl181drv_job_tail];
            pl181drv_job_tail = (pl181drv_job_tail + 1) % PL181DRV_JOB_QUEUE_SZ;

            switch (job.cmd) {
            case SD_CMD_READ: {
                int rc = pl181_read_block(job.arg);
                _reply((uint32_t)job.reply_h, rc == 0 ? 0 : (uint32_t)-1, 0, 0);
                break;
            }
            case SD_CMD_WRITE: {
                int rc = pl181_write_block(job.arg);
                _reply((uint32_t)job.reply_h, rc == 0 ? 0 : (uint32_t)-1, 0, 0);
                break;
            }
            default:
                _reply((uint32_t)job.reply_h, (uint32_t)-1, 0, 0);
                break;
            }
        }
    }
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
        int32_t granted = _port_grant(shmem_handle, (int32_t)sender);
        if (granted < 0)
            _reply(reply_h, (uint32_t)-1, 0, 0);
        else
            _reply(reply_h, 0, (uint32_t)granted, 0);
        break;
    }

    case SD_CMD_READ:
    {
        unsigned next = (pl181drv_job_head + 1) % PL181DRV_JOB_QUEUE_SZ;
        if (next == pl181drv_job_tail) {
            _reply(reply_h, (uint32_t)-1, 0, 0);
        } else {
            pl181drv_jobs[pl181drv_job_head].cmd = SD_CMD_READ;
            pl181drv_jobs[pl181drv_job_head].arg = arg;
            pl181drv_jobs[pl181drv_job_head].reply_h = reply_h;
            pl181drv_jobs[pl181drv_job_head].sender = sender;
            pl181drv_job_head = next;
            (void)_ntfn_signal((uint32_t)pl181drv_worker_ntfn, 1);
        }
        break;
    }

    case SD_CMD_WRITE:
    {
        unsigned nextw = (pl181drv_job_head + 1) % PL181DRV_JOB_QUEUE_SZ;
        if (nextw == pl181drv_job_tail) {
            _reply(reply_h, (uint32_t)-1, 0, 0);
        } else {
            pl181drv_jobs[pl181drv_job_head].cmd = SD_CMD_WRITE;
            pl181drv_jobs[pl181drv_job_head].arg = arg;
            pl181drv_jobs[pl181drv_job_head].reply_h = reply_h;
            pl181drv_jobs[pl181drv_job_head].sender = sender;
            pl181drv_job_head = nextw;
            (void)_ntfn_signal((uint32_t)pl181drv_worker_ntfn, 1);
        }
        break;
    }

    default:
        _reply(reply_h, (uint32_t)-1, 0, 0);
        break;
    }
}

static int pl181drv_setup(void)
{
    port = _port_create();
    if (port < 0)
    {
        printf("pl181drv: port_create failed\n");
        return -1;
    }

    int32_t nt_slot = _port_grant(port, NAMETABLE_PID);
    if (nt_slot < 0)
    {
        printf("pl181drv: nt grant failed\n");
        return -1;
    }

    /* locate devmgr */
    msg_t r = _call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
    if ((int32_t)r.r1 != NT_LU_OK)
    {
        printf("pl181drv: devmgr lookup failed\n");
        return -1;
    }
    int32_t devmgr_port = (int32_t)r.r2;

    /* request the block device capability */
    r = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_BLOCK, 0);
    if ((int32_t)r.r1 != 0)
    {
        printf("pl181drv: block device request failed\n");
        return -1;
    }
    block_dev_handle = (int32_t)r.r2;

    block_irq_ntfn = _ntfn_create();
    if (block_irq_ntfn < 0)
    {
        printf("pl181drv: ntfn_create failed\n");
        return -1;
    }

    if (_irq_claim((uint32_t)block_dev_handle) < 0)
    {
        printf("pl181drv: irq_claim failed\n");
        return -1;
    }
    if (_irq_bind((uint32_t)block_dev_handle, (uint32_t)block_irq_ntfn) < 0)
    {
        printf("pl181drv: irq_bind failed\n");
        return -1;
    }

    pl181 = (pl181_t *)_mapdev((uint32_t)block_dev_handle);
    if ((intptr_t)pl181 <= 0)
    {
        printf("pl181drv: mapdev failed\n");
        return -1;
    }

    uint32_t pid0 = pl181->PERIPHID[0] & 0xFF;
    uint32_t pid1 = pl181->PERIPHID[1] & 0xFF;
    if (!((pid0 == 0x80 || pid0 == 0x81) && pid1 == 0x11))
    {
        printf("pl181drv: unexpected peripheral ID %02x %02x\n", pid0, pid1);
        return -1;
    }
    printf("pl181drv: detected PL18%u\n", pid0 == 0x80 ? 0 : 1);

    if (pl181_setup() < 0)
        return -1;

    shmem_result_t shm = _memshare(4096);
    if (shm.handle < 0 || shm.addr == NULL)
    {
        printf("pl181drv: shmem failed\n");
        return -1;
    }
    shmem_handle = shm.handle;
    shmem_buf = (uint32_t *)shm.addr;

    msg_t den_r = _call(NT_PORT, DEN_MYDEN, 0, 0);
    uint32_t my_den = (den_r.r1 == DEN_OK) ? den_r.r2 : 0;

    (void)_send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack("pl181drv"), (uint32_t)nt_slot);

    pl181drv_worker_ntfn = _ntfn_create();
    if (pl181drv_worker_ntfn < 0) {
        printf("pl181drv: worker ntfn create failed\n");
        return -1;
    }

    void *wstack = malloc(USER_STACK_SIZE);
    if (!wstack) {
        printf("pl181drv: worker stack alloc failed\n");
        return -1;
    }

    tid_t wt = _tmake(pl181drv_worker, (void *)((char *)wstack + USER_STACK_SIZE), NULL);
    if ((int)wt < 0) {
        printf("pl181drv: worker spawn failed\n");
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
        int32_t bits = _ntfn_poll((uint32_t)block_irq_ntfn);
        if (bits > 0)
        {
            _irq_done((uint32_t)block_dev_handle);
        }

        msg_t msg = _recv(port);

        if ((int32_t)msg.r0 > 0)
        {
            handle_client(msg);
        }
    }
}
