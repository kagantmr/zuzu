#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/fat32d_protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include "ff.h"

static FATFS fs;
static shmem_result_t shm;

int handle_readdir(uint32_t arg) {
    // TODO
    return -1;
}

int handle_stat(uint32_t arg) {
    // TODO
    return -1;
}


void handle_request(uint32_t reply_h, uint32_t sender, uint32_t cmd, uint32_t arg) {

    int res = 0;
    switch (cmd) {
        case FAT32_READDIR:
            res = handle_readdir(arg);
            break;
        case FAT32_STAT:
            res = handle_stat(arg);
            break;
        case FAT32_GET_BUF:
            int32_t slot = _port_grant(shm.handle, (int32_t)sender);
            if (slot < 0) {
                res = -1;
            } else {
                // reply with the slot so fbox can _attach it
                _reply(reply_h, 0, (uint32_t)slot, 0);
                return;
            }
            break;
        default:
            res = -1; // unknown command
    }
    _reply(reply_h, (uint32_t)res, 0, 0);
}

FIL fil_table[16];
DIR dir_table[8];
bool fil_used[16];
bool dir_used[8];

int main(void) {
    zuzu_ipcmsg_t r;

    // ask which den we're in
    r = _call(NT_PORT, DEN_MYDEN, 0, 0);
    if (r.r1 != DEN_OK) {
        printf("fat32d: not in any den\n");
        return 1;
    }
    uint32_t my_den = r.r2;

    // look up zusd who should be in the same den
    r = _call(NT_PORT, NT_LOOKUP, nt_pack("zusd"), 0);
    if (r.r1 != NT_LU_OK) {
        printf("fat32d: cannot see zusd\n");
        return 1;
    }

    // register ourselves into the den
    int32_t my_port = _port_create();
    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    _send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack("fat3"), (uint32_t)nt_slot);

    // now verify zzsh can't see us - we can't test that from here,
    // but we can sit and serve
    printf("fat32d: ready\n");
    
    FRESULT fr;

    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %d\n", fr);
        return 1;
    }
    shm = _memshare(4096);

    while (1) {
        zuzu_ipcmsg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}