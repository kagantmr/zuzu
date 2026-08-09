#include <zuzu/service.h>
#include "zuzu/protocols/nametable.h"
#include <zuzu/zuzu.h>
#include <string.h>

int32_t register_service(const char *name) {
    int32_t port = ZuzuPortCreate();
    if (port < 0)
        return -1;

    int32_t nt_slot = ZuzuGrant(port, NAMETABLE_PID);
    if (nt_slot < 0)
        return -1;

    /* announce ourselves */
    (void)ZuzuMsgSend(NT_PORT, NT_REGISTER, nt_pack(name), (uint32_t)nt_slot);

    return port;
}

int32_t lookup_service(const char *name) {
    while (1) {
        Message reply = ZuzuMsgCall(NT_PORT, NT_LOOKUP, nt_pack(name), 0);
        if (reply.w1 == NT_LU_OK)
            return (int32_t)reply.w2;

        if (strcmp(name, "tty0") == 0) {
            reply = ZuzuMsgCall(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
            if (reply.w1 == NT_LU_OK)
                return (int32_t)reply.w2;
        }

        ZuzuSleep(10);
    }
}