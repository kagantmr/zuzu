#include "initrd.h"
#include <assert.h>
#include <convert.h>
#include <cpio.h>
#include <string.h>

static const void *initrd_base;
static size_t initrd_size;

void InitrdInit(const void *start, size_t size)
{
    assert(size > sizeof(cpio_hdr_t));
    initrd_base = start;
    initrd_size = size;
}

bool InitrdFind(const char *name, const void **data_out, size_t *size_out)
{
    return CpioFind(initrd_base, initrd_size, name, data_out, size_out);
}
