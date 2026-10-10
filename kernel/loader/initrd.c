#include "initrd.h"
#include <assert.h>
#include <convert.h>
#include <cpio.h>
#include <string.h>

static const void *initrd_base;
static size_t initrd_size;

void InitrdInit(const void *start, size_t size)
{
    assert(size > sizeof(cpio_hdr_t)); // must be at least large enough to hold one header
    initrd_base = start;               // just remember where the archive is
    initrd_size = size;                // that's it. no walking, no parsing.
}

bool InitrdFind(const char *name, const void **data_out, size_t *size_out)
{
    return CpioFind(initrd_base, initrd_size, name, data_out, size_out);
}