
#include <cpio.h>
#include <string.h>
#include <mem.h>
#include <stdint.h>

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


bool cpio_find(const void *base, size_t size, const char *name,
               const void **data_out, size_t *size_out)
                {
    const uint8_t *ptr = base;
    const uint8_t *end = base + size;

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