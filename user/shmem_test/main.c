// user/shmem_test/main.c
#include "zuzu.h"

int main(void) {
    volatile uint32_t dummy = 0; (void)dummy; // warm the stack frame
    uint32_t before = _pmm_free();

    shmem_result_t result = _memshare(4096);
    void *addr = result.addr;
    if ((int32_t)(uintptr_t)addr < 0) {
        _log("shmem_test: memshare failed\n", 29);
        return 1;
    }

    uint32_t after = _pmm_free();

    volatile uint32_t *buf = (volatile uint32_t *)addr;
    buf[0] = 0xDEADBEEF;
    buf[1] = 0xCAFEBABE;

    if (buf[0] == 0xDEADBEEF && buf[1] == 0xCAFEBABE)
        _log("shmem_test: read/write OK\n", 26);
    else
        _log("shmem_test: FAIL\n", 17);


    // replace the PMM check with this temporarily
    char msg[] = "shmem_test: PMM delta: X\n";
    uint32_t delta = before - after;
    msg[23] = '0' + (delta < 10 ? delta : '?');
    _log(msg, 25);

    // verify PMM actually consumed pages (1 data + 1 L2 table on first mapping)
    if (before - after >= 1)
        _log("shmem_test: PMM consumed pages OK\n", 35);
    else
        _log("shmem_test: PMM page count wrong\n", 33);

    return 0;
}
