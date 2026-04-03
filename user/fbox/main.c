#include <zuzu/protocols/fat32d_protocol.h>
#include <zuzu/protocols/fbox_protocol.h>
#include <zuzu/protocols/nt_protocol.h>
#include <service.h>
#include <zuzu.h>
#include <stdio.h>

static int32_t fat32d_port = -1;
static shmem_result_t fat32d_shm;
static void *fat32d_buf = NULL;
static shmem_result_t my_shm;

void handle_request(uint32_t reply_h, uint32_t sender,
                    uint32_t cmd, uint32_t arg) {
    switch (cmd) {
    case FBOX_GET_BUF: {
        int32_t slot = _port_grant(my_shm.handle, (int32_t)sender);
        if (slot < 0)
            _reply(reply_h, (uint32_t)-1, 0, 0);
        else
            _reply(reply_h, 0, (uint32_t)slot, 0);
        break;
    }
    default:
        _reply(reply_h, (uint32_t)-1, 0, 0);
        break;
    }
}

int main(void) {
    // register in disk den (auto-detected by register_service)
    int32_t my_port = register_service("fbox");
    if (my_port < 0) {
        printf("fbox: register failed\n");
        return 1;
    }

    // also register in global den (0) so clients can find us
    int32_t global_slot = _port_grant(my_port, NAMETABLE_PID);
    if (global_slot < 0) {
        printf("fbox: global grant failed\n");
        return 1;
    }
    _send(NT_PORT, NT_REGISTER | (0 << 8), nt_pack("fbox"), (uint32_t)global_slot);

    // look up fat32d
    fat32d_port = lookup_service("fat3");
    if (fat32d_port < 0) {
        printf("fbox: cannot find fat32d\n");
        return 1;
    }

    // get fat32d's shmem buffer
    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("fbox: FAT32_GET_BUF failed\n");
        return 1;
    }
    fat32d_buf = _attach((int32_t)r.r2);
    if ((intptr_t)fat32d_buf <= 0) {
        printf("fbox: attach failed\n");
        return 1;
    }

    // allocate our own shmem for clients
    my_shm = _memshare(4096);
    if (my_shm.handle < 0 || !my_shm.addr) {
        printf("fbox: shmem failed\n");
        return 1;
    }

    printf("fbox: ready\n");

    while (1) {
        zuzu_ipcmsg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}