#include "zuzu.h"
#include <zmalloc.h>
#include <snprintf.h>
#include "zuzu/protocols/nt_protocol.h"

#define TEST_COUNT 1000

int main(void)
{
    // try registering to zuzusysd's name service
    int32_t reg_port = _port_create();
    if (reg_port >= 0) {
        int32_t nt_slot = _port_grant(reg_port, NAMETABLE_PID);
        if (nt_slot >= 0) {
            _send(NT_PORT, NT_REGISTER, nt_pack("test"), (uint32_t)nt_slot);
            zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("test"), 0);
            if (reply.r1 == NT_LU_OK) {
                _log("Lookup successful\n", 18);
            } else {
                _log("Lookup failed\n", 15);
            }
        }
    }

    _quit(0);
}