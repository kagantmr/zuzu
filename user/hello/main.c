#include "zuzu.h"
#include "nt_protocol.h"
#include "zuart_protocol.h"

int main(void)
{
    _sleep(10);
    zuzu_ipcmsg_t reply = _call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
    if (reply.r1 != 0)
    {
        _log("hello: uart lookup failed\n", 26);
        return 1;
    }

    int uart_handle = reply.r2; // slot in OUR table

    // send one char at a time... for now
    while (1)
    {
        const char *msg = "hello: i'm still awake\n";
        for (const char *p = msg; *p; p++)
        {
            _call(uart_handle, ZUART_CMD_WRITE, *p, 0);
        }
        _sleep(1000);
    }

    return 42;
}