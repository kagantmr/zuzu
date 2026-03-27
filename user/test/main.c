#include "zuzu.h"
#include <zmalloc.h>
#include <snprintf.h>
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/zusd_protocol.h"
#include <stdio.h>

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
                printf("Lookup successful\n");
            } else {
                printf("Lookup failed\n");
            }
        }
    }

        printf("test: looking up zusd\n");

    zuzu_ipcmsg_t r = _call(NT_PORT, NT_LOOKUP, nt_pack("zusd"), 0);
    if ((int32_t)r.r1 != NT_LU_OK) {
        printf("test: zusd not found\n");
        return 1;
    }
    int32_t zusd_port = (int32_t)r.r2;

    /* get the shared 512-byte buffer */
    r = _call(zusd_port, ZUSD_CMD_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("test: GET_BUF failed\n");
        return 1;
    }
    int32_t buf_handle = (int32_t)r.r2;

    uint8_t *buf = (uint8_t *)_attach(buf_handle);
    if ((intptr_t)buf <= 0) {
        printf("test: attach failed\n");
        return 1;
    }

    /* read block 0 */
    r = _call(zusd_port, ZUSD_CMD_READ, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("test: READ block 0 failed\n");
        return 1;
    }

    /* print first 16 bytes */
    printf("test: block 0 first 16 bytes:\n  ");
    for (int i = 0; i < 16; i++)
        printf("%02x ", buf[i]);
    printf("\n");

    /* check MBR signature at bytes 510-511 */
    uint8_t sig0 = buf[510];
    uint8_t sig1 = buf[511];
    printf("test: MBR sig = %02x %02x (%s)\n",
           sig0, sig1,
           (sig0 == 0x55 && sig1 == 0xAA) ? "VALID" : "INVALID");

    /* read block 1 to sanity-check sequential access */
    r = _call(zusd_port, ZUSD_CMD_READ, 0, 1);
    if ((int32_t)r.r1 != 0) {
        printf("test: READ block 1 failed\n");
        return 1;
    }
    printf("test: block 1 first 4 bytes: %02x %02x %02x %02x\n",
           buf[0], buf[1], buf[2], buf[3]);

    printf("test: PASS\n");
    return 0;
}