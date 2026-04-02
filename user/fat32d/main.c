#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/ipcx.h"
#include <stdio.h>

static int32_t zuart_port;

int main(void) {
    // zuart is global, always visible
    zuzu_ipcmsg_t r;

    // ask which den we're in
    r = _call(NT_PORT, DEN_MYDEN, 0, 0);
    if (r.r1 != DEN_OK) {
        printf("fat32d: not in any den\n");
        return 1;
    }
    uint32_t my_den = r.r2;
    printf("fat32d: assigned to den\n");

    // look up zusd — should be in the same den
    r = _call(NT_PORT, NT_LOOKUP, nt_pack("zusd"), 0);
    if (r.r1 == NT_LU_OK) {
        printf("fat32d: PASS - can see zusd\n");
    } else {
        printf("fat32d: FAIL - cannot see zusd\n");
        return 1;
    }

    // register ourselves into the den
    int32_t my_port = _port_create();
    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    _send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack("fat3"), (uint32_t)nt_slot);
    printf("fat32d: registered in den\n");

    // now verify zzsh can't see us — we can't test that from here,
    // but we can sit and serve
    printf("fat32d: ready\n");

    while (1) {
        _recv(my_port);
    }
}