#include <zuzu.h>
#include "zusd.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"

pl181_t *pl181;

int main() {
    // register to name service
    int32_t reg_port = _port_create();
    if (reg_port < 0) {
        _log("zusd: failed to create port\n",28);
        return 1;
    }
    int32_t nt_slot = _port_grant(reg_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        _log("zusd: failed to grant port\n" , 27);
        return 1;
    }
    (void)_send(NT_PORT, NT_REGISTER, nt_pack("zusd"), (uint32_t)nt_slot);

    // grab devmgr handle from name service
    zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
    if (reply.r1 != NT_LU_OK) {
        _log("zusd: failed to lookup devmgr\n", 30);
        return 1;
    }
    int32_t devmgr_port = (int32_t)reply.r2;

    // query devmgr for block device handle
    reply = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_BLOCK, 0);
    if (reply.r0 != 0) {
        _log("zusd: failed to request block device\n", 36);
        return 1;
    }
    int32_t block_port = (int32_t)reply.r2;

    // map it into memory
    pl181 = _mapdev(block_port);
    if (!pl181) {
        _log("zusd: failed to map block device\n", 33);
        return 1;
    }

    uint32_t id0 = pl181->PERIPHID[0];
    uint32_t id1 = pl181->PERIPHID[1];
    // Common variants report PERIPHID0 as 0x80 (PL180) or 0x81 (PL181), with PERIPHID1 = 0x11.
    uint32_t pid0 = id0 & 0xFF;
    uint32_t pid1 = id1 & 0xFF;
    if (!((pid0 == 0x80 || pid0 == 0x81) && pid1 == 0x11)) {
        _log("zusd: unexpected peripheral ID\n", 31);
        return 1;
    }
    _log("zusd: PL18x found\n", 18);

    return 0;
}