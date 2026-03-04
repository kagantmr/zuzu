#include "zuzu.h"
#include "nt_protocol.h"
#include "zuart_protocol.h"

static void zuart_write_str(int handle, const char *s)
{
    for (const char *p = s; *p; p++)
    {
        _call(handle, ZUART_CMD_WRITE, (uint32_t)(unsigned char)*p, 0);
    }
}

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

    // blocking-read test: this call should block until a byte is received by zuart
    zuart_write_str(uart_handle, "hello: type one key for blocking read test...\n");
    zuzu_ipcmsg_t read_reply = _call(uart_handle, ZUART_CMD_READ, 0, 0);
    if ((int32_t)read_reply.r2 == 0)
    {
        _call(uart_handle, ZUART_CMD_WRITE, read_reply.r1, 0);
        zuart_write_str(uart_handle, " <- hello got your key\n");
    }
    else
    {
        zuart_write_str(uart_handle, "hello: read failed\n");
    }

    // periodic writer
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
