#include "initrd.h"
#include <mem.h>
#include <convert.h>
#include <string.h>

#include <assert.h>

static uint32_t parse_hex8(const char *s) {
    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9')
            val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f')
            val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val = (val << 4) | (c - 'A' + 10);
    }
    return val;
}

static const void *initrd_base;
static size_t      initrd_size;

void initrd_init(const void *start, size_t size) {
    kassert(sizeof(cpio_hdr_t) == 110); // must be at least large enough to hold one header
    initrd_base = start;     // just remember where the archive is
    initrd_size = size;      // that's it. no walking, no parsing.
}

bool initrd_find(const char *name, const void **data_out, size_t *size_out) {

    const uint8_t *ptr = initrd_base;
    const uint8_t *end = initrd_base + initrd_size;

    while (ptr + sizeof(cpio_hdr_t) <= end) {
        
        const cpio_hdr_t *hdr = (const cpio_hdr_t *)ptr;

        // validate magic
        if (memcmp(hdr->magic, "070701", 6) != 0)
            return false;

        uint32_t namesize = parse_hex8(hdr->namesize);
        uint32_t filesize = parse_hex8(hdr->filesize);

        const char *entry_name = (const char *)(ptr + sizeof(cpio_hdr_t));
        const char *cmp_name = entry_name;
        if (cmp_name[0] == '.' && cmp_name[1] == '/')
            cmp_name += 2;

        // sentinel = end of archive
        if (memcmp(entry_name, "TRAILER!!!", 10) == 0)
            return false;

        // data starts after header + name, aligned to 4 bytes
        const uint8_t *data = ptr + align_up(sizeof(cpio_hdr_t) + namesize, 4);

        // is this the file we want?
        if (strcmp(cmp_name, name) == 0) {
            *data_out = data;
            *size_out = filesize;
            return true;
        }

        // advance to next entry
        ptr = data + align_up(filesize, 4);
    }
    return false;
}