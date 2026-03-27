#include <zuzu.h>
#include <stdbool.h>
#include <stdio.h>
#include "zusd.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"

pl181_t *pl181;
bool is_v2, is_sdhc;

static void pl181_delay(uint32_t iterations) {
    volatile uint32_t i;
    for (i = 0; i < iterations; i++) {
        __asm__ volatile("nop");
    }
}

int pl181_send_cmd(uint32_t cmd_index, uint32_t arg, uint32_t flags) {
    const uint32_t cmd_clear_mask = CMDCRCFAIL | CMDTIMEOUT | CMDSENT | CMDRESPEND | DATAEND | DATABLOCKEND;
    pl181->CLEAR = cmd_clear_mask;
    uint32_t attempts = 2000;
    pl181->ARGUMENT = arg;
    pl181->COMMAND = (cmd_index & 0x3F) | flags | MCI_CMD_ENABLE;
    
    uint32_t wait_mask = (flags & MCI_CMD_RESPONSE) ? CMDRESPEND : (CMDSENT | CMDRESPEND);

    while (attempts-- > 0) {
        uint32_t status = pl181->STATUS;
        if (status & wait_mask) {
            pl181->CLEAR = status & wait_mask;
            return 0;
        }
        if (status & CMDTIMEOUT) {
            printf("zusd: CMD%u timeout, STATUS=0x%08x\n", cmd_index, status);
            return -1;
        }
        if ((flags & MCI_CMD_RESPONSE) && (status & CMDCRCFAIL)) {
            // CRC fail is meaningful only for response commands.
            printf("zusd: CMD%u CRC fail, STATUS=0x%08x\n", cmd_index, status);
            return -1;
        }

        pl181_delay(200);
    }

    if (!(pl181->STATUS & wait_mask)) {
        printf("zusd: CMD%u wait bit timeout, STATUS=0x%08x\n", cmd_index, pl181->STATUS);
        return -1;
    }

    pl181->CLEAR = pl181->STATUS & wait_mask;
    return 0;
}

int pl181_setup(void) {
    // PL18x bringup
    pl181->POWER = MCI_POWER_UP | MCI_POWER_OPENDRAIN; // power up
    pl181->CLOCK = (1 << 8) | 0x1D; // enable clock, ~400kHz init frequency
    pl181_delay(1000000); // settle power/clock before first command
    pl181->POWER = MCI_POWER_ON | MCI_POWER_OPENDRAIN; // power on
    pl181_delay(500000); // additional stabilization delay

    // send cmd0
    int cmd0 = pl181_send_cmd(0, 0, 0);
    if (cmd0 == -1) {
        printf("zusd: CMD0 timed out, STATUS=0x%08x\n", pl181->STATUS);
        return -1;
    }

    // send cmd8 to learn supported type and sd card type
    int cmd8 = pl181_send_cmd(8, 0x000001AA, MCI_CMD_RESPONSE);
    if (cmd8 == 0) {
        // card responded, check the echo
        uint32_t resp = pl181->RESPONSE[0];
        if ((resp & 0xFFF) != 0x1AA) {
            printf("zusd: Voltage mismatch or bad card\n");
            return -1; // voltage mismatch or bad card
        }
        is_v2 = true;
    } else {
        // timeout — v1.x card, no SDHC support
        is_v2 = false;
    }

    uint32_t acmd41_arg = 0x00FF8000;
    if (is_v2) acmd41_arg |= (1u << 30);

    uint32_t ocr = 0;
    int retries = 1000;
    while (retries-- > 0) {
        // first send CMD55
        if (pl181_send_cmd(55, 0, MCI_CMD_RESPONSE) != 0) {
            printf("zusd: CMD55 failed\n");
            return -1;
        }
        // then send ACMD41
        if (pl181_send_cmd(41, acmd41_arg, MCI_CMD_RESPONSE) != 0) {
            printf("zusd: ACMD41 failed\n");
            return -1;
        }
        ocr = pl181->RESPONSE[0];
        if (ocr & (1u << 31)) {
            break; // card is ready
        }
        pl181_delay(50000); // short retry backoff without scheduler dependency
    }

    if (!(ocr & (1u << 31))) {
        printf("zusd: Card initialization timed out\n");
        return -1; // card never became ready
    }

    is_sdhc = (ocr & (1u << 30)) != 0;

    // card identification, required wont progress without it. we dont save anything
    if (pl181_send_cmd(2, 0, MCI_CMD_RESPONSE | MCI_CMD_LONGRESP) != 0) {
        printf("zusd: Failed to identify card\n");
        return -1;
    }

    uint32_t rca;

    // ask for then save relative address
    if (pl181_send_cmd(3, 0, MCI_CMD_RESPONSE) == 0) {
        rca = pl181->RESPONSE[0] >> 16;
    } else {
        printf("zusd: Failed to get RCA\n");
        return -1;
    }

    // put card to transfer state
    if (pl181_send_cmd(7, rca << 16, MCI_CMD_RESPONSE) == 0) {
        printf("zusd: SD card ready for transfer\n");
    } else {
        return -1;
    }

    return 0;
}

int main() {
    printf("zusd: starting up\n");
    // register to name service
    int32_t reg_port = _port_create();
    if (reg_port < 0) {
        printf("zusd: failed to create port\n");
        return 1;
    }
    int32_t nt_slot = _port_grant(reg_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        printf("zusd: failed to grant port\n");
        return 1;
    }
    (void)_send(NT_PORT, NT_REGISTER, nt_pack("zusd"), (uint32_t)nt_slot);

    // grab devmgr handle from name service
    zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
    if (reply.r1 != NT_LU_OK) {
        printf("zusd: failed to lookup devmgr\n");
        return 1;
    }
    int32_t devmgr_port = (int32_t)reply.r2;

    // query devmgr for block device handle
    reply = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_BLOCK, 0);
    if ((int32_t)reply.r1 != 0) {
        printf("zusd: failed to request block device\n");
        return 1;
    }
    int32_t block_port = (int32_t)reply.r2;

    // map it into memory
    pl181 = _mapdev(block_port);
    if ((intptr_t)pl181 <= 0) {
        printf("zusd: failed to map block device\n");
        return 1;
    }

    uint32_t id0 = pl181->PERIPHID[0];
    uint32_t id1 = pl181->PERIPHID[1];
    // Common variants report PERIPHID0 as 0x80 (PL180) or 0x81 (PL181), with PERIPHID1 = 0x11
    uint32_t pid0 = id0 & 0xFF;
    uint32_t pid1 = id1 & 0xFF;
    if (!((pid0 == 0x80 || pid0 == 0x81) && pid1 == 0x11)) {
        printf("zusd: unexpected peripheral ID\n");
        return 1;
    }

    printf("zusd: detected PL18%u variant\n", (pid0 == 0x80) ? 0 : 1);

    if (pl181_setup() == -1) {
        printf("zusd: failed to setup PL181\n");
        return -1;
    }

    while (1);
}