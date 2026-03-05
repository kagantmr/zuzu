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


#define PROT_READ  0x1
#define PROT_WRITE 0x2

static void test_demand_paging(int uart_handle)
{
    zuart_write_str(uart_handle, "demand: starting 1MB test\n");

    uint32_t before = _pmm_free();

    // allocate 1MB — no pages should be consumed yet (lazy)
    uint8_t *buf = (uint8_t *)_memmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE);
    if ((int32_t)(uintptr_t)buf < 0) {
        zuart_write_str(uart_handle, "demand: mmap FAILED\n");
        return;
    }

    uint32_t after_mmap = _pmm_free();

    // touch only first and last page
    buf[0]               = 0xAA;
    buf[1024 * 1024 - 1] = 0xBB;

    uint32_t after_touch = _pmm_free();

    // verify the values survived
    int ok = (buf[0] == 0xAA) && (buf[1024 * 1024 - 1] == 0xBB);

    // print results
    zuart_write_str(uart_handle, ok
        ? "demand: readback OK\n"
        : "demand: readback FAILED\n");

    // before and after_mmap should be equal — mmap itself costs nothing
    zuart_write_str(uart_handle, before == after_mmap
        ? "demand: lazy alloc confirmed (mmap cost 0 pages)\n"
        : "demand: FAIL mmap consumed pages early\n");

    // exactly 2 pages should have been faulted in
    uint32_t consumed = after_mmap - after_touch;
    zuart_write_str(uart_handle, consumed == 2
        ? "demand: exactly 2 pages consumed OK\n"
        : "demand: WRONG page count\n");

    _memunmap(buf, 1024 * 1024);

    uint32_t after_unmap = _pmm_free();
    zuart_write_str(uart_handle, after_unmap == before
        ? "demand: pages returned to PMM OK\n"
        : "demand: page leak detected\n");
}


static void test_memmap(int uart_handle)
{
    zuart_write_str(uart_handle, "memmap: starting basic alloc test\n");

    /* allocate one page — let kernel pick the VA */
    void *buf = _memmap(NULL, 4096, PROT_READ | PROT_WRITE);
    if ((int32_t)buf < 0) {
        zuart_write_str(uart_handle, "memmap: FAILED to allocate\n");
        return;
    }

    /* write a known pattern across the whole page */
    uint8_t *p = (uint8_t *)buf;
    for (int i = 0; i < 4096; i++)
        p[i] = (uint8_t)(i & 0xFF);

    /* read it back and verify */
    int ok = 1;
    for (int i = 0; i < 4096; i++) {
        if (p[i] != (uint8_t)(i & 0xFF)) {
            ok = 0;
            break;
        }
    }

    zuart_write_str(uart_handle, ok
        ? "memmap: write/read verified OK\n"
        : "memmap: MISMATCH detected\n");

    /* write a visible string into the buffer and print it via uart */
    const char *msg = "hello from memmap'd memory!";
    uint8_t *s = (uint8_t *)buf;
    for (int i = 0; msg[i]; i++)
        s[i] = msg[i];
    s[27] = '\n';
    s[28] = '\0';
    zuart_write_str(uart_handle, (const char *)buf);

    /* clean up */
    int ret = _memunmap(buf, 4096);
    zuart_write_str(uart_handle, ret == 0
        ? "memmap: munmap OK\n"
        : "memmap: munmap FAILED\n");
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
    test_memmap(uart_handle);
    test_demand_paging(uart_handle);
    zuart_write_str(uart_handle, "hello: type one key...\n");

    // blocking-read test: this call should block until a byte is received by zuart
    zuart_write_str(uart_handle, "hello: type one key for blocking read test...\n");
    zuzu_ipcmsg_t read_reply = _call(uart_handle, ZUART_CMD_READ, 0, 0);
    if ((int32_t)read_reply.r2 == 0)
    {
        //_call(uart_handle, ZUART_CMD_WRITE, read_reply.r1, 0);
        zuart_write_str(uart_handle, " <- hello got your key\n");
    }
    else
    {
        zuart_write_str(uart_handle, "hello: read failed\n");
    }

    
    // ... rest unchanged

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
