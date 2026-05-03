#include "initrd.h"
#include <mem.h>
#include <convert.h>
#include <string.h>
#include <cpio.h>
#include <assert.h>


static const void *initrd_base;
static size_t      initrd_size;

void initrd_init(const void *start, size_t size) {
    assert(sizeof(cpio_hdr_t) == 110); // must be at least large enough to hold one header
    initrd_base = start;     // just remember where the archive is
    initrd_size = size;      // that's it. no walking, no parsing.
}

bool initrd_find(const char *name, const void **data_out, size_t *size_out) {
    return cpio_find(initrd_base, initrd_size, name, data_out, size_out);
}