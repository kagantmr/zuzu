#include "zuzu.h"
#include <zmalloc.h>
#include <snprintf.h>

#define TEST_COUNT 1000

int main(void)
{
    void *ptrs[TEST_COUNT];
    uint32_t before, after_alloc, after_free, after_reuse;
    char buf[64];
    int len;

    before = _pmm_free();

    // Round 1: allocate varied sizes
    for (int i = 0; i < TEST_COUNT; i++)
        ptrs[i] = zmalloc(8 + (i % 128) * 8);

    after_alloc = _pmm_free();

    // Free everything
    for (int i = 0; i < TEST_COUNT; i++)
        zfree(ptrs[i]);

    after_free = _pmm_free();

    // Round 2: allocate again — should reuse free list, no new pages
    for (int i = 0; i < TEST_COUNT; i++)
        ptrs[i] = zmalloc(8 + (i % 128) * 8);

    after_reuse = _pmm_free();

    // Free again
    for (int i = 0; i < TEST_COUNT; i++)
        zfree(ptrs[i]);

    len = snprintf(buf, sizeof(buf), "before:  %u\n", before);
    _log(buf, len);
    len = snprintf(buf, sizeof(buf), "alloc1:  %u\n", after_alloc);
    _log(buf, len);
    len = snprintf(buf, sizeof(buf), "freed:   %u\n", after_free);
    _log(buf, len);
    len = snprintf(buf, sizeof(buf), "alloc2:  %u\n", after_reuse);
    _log(buf, len);

    if (after_reuse == after_alloc)
        _log("PASS: no leak\n", 14);
    else
        _log("FAIL: leak detected\n", 20);

    _quit(0);
}