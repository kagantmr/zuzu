#include <zuzu/service.h>
#include "zuzu/protocols/nt_protocol.h"
#include <zuzu/zuzu.h>
#include <string.h>

int32_t register_service(const char *name) {
    int32_t port = zuzu_port_create();
    if (port < 0)
        return -1;

    int32_t nt_slot = zuzu_grant(port, NAMETABLE_PID);
    if (nt_slot < 0)
        return -1;

    /* ask sysd which den we belong to */
    msg_t den_r = zuzu_msg_call(NT_PORT, DEN_MYDEN, 0, 0);
    uint32_t my_den = (den_r.r1 == DEN_OK) ? den_r.r2 : 0;

    /* announce ourselves */
    (void)zuzu_msg_send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack(name), (uint32_t)nt_slot);

    return port;
}

int32_t lookup_service(const char *name) {
    while (1) {
        msg_t reply = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack(name), 0);
        if (reply.r1 == NT_LU_OK)
            return (int32_t)reply.r2;

        if (strcmp(name, "tty0") == 0) {
            reply = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
            if (reply.r1 == NT_LU_OK)
                return (int32_t)reply.r2;
        }

        zuzu_sleep(10);
    }
}