// dtb_parser.c
#include "kernel/dtb/dtb.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib/mem.h"
#include "lib/string.h"
#include "core/assert.h"

static inline void halt(void) {
    while (1) {
        __asm__ volatile ("wfi");
    };
}

typedef enum FDT_CODE {
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE   = 2,
    FDT_PROP       = 3,
    FDT_NOP        = 4,
    FDT_END        = 9
} FDT_CODE;

static dtb_node_t node_pool[512];
static size_t allocated = 0;

static const dtb_property_t* find_prop(const dtb_node_t* node, const char* name) {
    kassert(node != NULL);
    kassert(name != NULL);
    for (uint32_t i = 0; i < node->property_count; i++) {
        if (strcmp(node->properties[i].name, name) == 0) {
            return &node->properties[i];
        }
    }
    return NULL;
}

static dtb_node_t* dtb_new_node(void) {
    kassert(allocated < (sizeof(node_pool)/sizeof(node_pool[0])));
    if (allocated >= (sizeof(node_pool)/sizeof(node_pool[0]))) {
        halt();
    }
    dtb_node_t *n = &node_pool[allocated++];

    // initialize fields to safe defaults
    n->name = NULL;
    n->property_count = 0;
    n->child_count = 0;
    // zero children and properties to be safe
    for (size_t i = 0; i < DTB_MAX_CHILDREN; ++i) n->children[i] = NULL;
    for (size_t i = 0; i < DTB_MAX_PROPS; ++i) {
        n->properties[i].name = NULL;
        n->properties[i].value = NULL;
        n->properties[i].length = 0;
    }
    return n;
}

// safe read of a big-endian u32 at cur (avoids unaligned dereference)
static inline uint32_t read_be32(const uint8_t* cur) {
    kassert(cur != NULL);
    uint32_t tmp;
    memcpy(&tmp, cur, sizeof(uint32_t));
    return bswap32(tmp);
}

dtb_node_t *dtb_parse(const void *dtb_data) {
    kassert(dtb_data != NULL);
    if ((uintptr_t) dtb_data == 0) halt();

    const dtb_header_t *header = (const dtb_header_t *)dtb_data;

    uint32_t magic = bswap32(header->magic);
    if (magic != 0xD00DFEEDu) {
        // invalid DTB magic
        halt();
    }

    uint32_t totalsize      = bswap32(header->totalsize);
    uint32_t struct_offset  = bswap32(header->off_dt_struct);
    uint32_t strings_offset = bswap32(header->off_dt_strings);
    // off_mem_rsvmap can be used later if needed

    const uint8_t* base = (const uint8_t*)dtb_data;
    const uint8_t* end  = base + totalsize;

    const uint8_t* struct_block  = base + struct_offset;
    const uint8_t* strings_block = base + strings_offset;

    // basic sanity
    if (struct_block < base || struct_block >= end) halt();
    if (strings_block < base || strings_block >= end) halt();

    const uint8_t* cur = struct_block;

    dtb_node_t* stack[64];
    size_t stack_top = 0; // number of elements on stack

    dtb_node_t* root = NULL;

    for (;;) {
        // check we can read a token
        if (cur + 4 > end) halt();
        uint32_t token = read_be32(cur);
        cur += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            // node name is a NUL-terminated C string at cur
            if (cur >= end) halt();
            const char* name = (const char*)cur;

            // ensure NUL exists before end
            const uint8_t* scan = cur;
            bool found_nul = false;
            while (scan < end) {
                if (*scan == '\0') { found_nul = true; break; }
                scan++;
            }
            if (!found_nul) halt();

            size_t name_len = (size_t)(scan - cur) + 1; // include '\0'
            cur += name_len;

            // align to 4 bytes
            while (((uintptr_t)cur & 3) != 0) cur++;

            // allocate node and set name (pointer into DTB strings in struct block)
            dtb_node_t* node = dtb_new_node();
            node->name = name;

            // attach to parent if any
            if (stack_top == 0) {
                // this is the root node
                root = node;
            } else {
                dtb_node_t* parent = stack[stack_top - 1];
                if (parent->child_count >= DTB_MAX_CHILDREN) {
                    // out of child slots
                    halt();
                }
                parent->children[parent->child_count++] = node;
            }

            // push
            if (stack_top >= sizeof(stack)/sizeof(stack[0])) halt();
            stack[stack_top++] = node;
        } break;

        case FDT_PROP: {
            // property: u32 len; u32 nameoff; u8 value[len]; pad to 4 bytes
            if (cur + 8 > end) halt();
            uint32_t len = read_be32(cur);
            cur += 4;
            uint32_t nameoff = read_be32(cur);
            cur += 4;

            // bounds check for nameoff within strings block
            const uint8_t* pname = strings_block + nameoff;
            if (pname < base || pname >= end) halt();
            // ensure NUL exists for property name (very small scan)
            const uint8_t* sscan = pname;
            bool found = false;
            while (sscan < end) {
                if (*sscan == '\0') { found = true; break; }
                sscan++;
            }
            if (!found) halt();

            const char* prop_name = (const char*)pname;

            // ensure value lies inside DTB
            if (cur + len > end) halt();
            const uint8_t* prop_val = cur;

            // add property into current node
            if (stack_top == 0) {
                // property outside any node? malformed
                halt();
            }
            dtb_node_t* cur_node = stack[stack_top - 1];
            if (cur_node->property_count >= DTB_MAX_PROPS) {
                // out of property slots
                halt();
            }
            dtb_property_t* prop = &cur_node->properties[cur_node->property_count++];
            prop->name = prop_name;
            prop->value = prop_val; // pointer into DTB; copy if you need owning memory
            prop->length = len;

            // advance past value and pad to 4 bytes
            cur += len;
            while (((uintptr_t)cur & 3) != 0) cur++;
        } break;

        case FDT_END_NODE: {
            if (stack_top == 0) {
                // unmatched end
                halt();
            }
            stack_top--; // pop
        } break;

        case FDT_NOP:
            // do nothing
            break;

        case FDT_END:
            return root;

        default:
            // unknown token
            halt();
        }
    }

    // unreachable
    return NULL;
}


static const dtb_node_t* dtb_find_node_rec(const dtb_node_t* node, const char* path) {
    if (*path == '\0')
        return node;

    if (*path == '/')
        path++;

    const char* slash = strchr(path, '/');
    size_t seg_len = slash ? (size_t)(slash - path) : strlen(path);

    for (uint32_t i = 0; i < node->child_count; i++) {
        const dtb_node_t* child = node->children[i];
        const char* name = child->name;
        
        if (!name) continue;

        // Match segment with child->name up to '@'
        if (strncmp(name, path, seg_len) == 0 &&
            (name[seg_len] == '\0' || name[seg_len] == '@'))
        {
            if (!slash)
                return child;
            return dtb_find_node_rec(child, slash + 1);
        }
    }

    return NULL;
}

const dtb_node_t* dtb_find_node(const dtb_node_t* root, const char* path) {
    kassert(path != NULL);
    if (!root || !path) return NULL;
    if (strcmp(path, "/") == 0) return root;
    return dtb_find_node_rec(root, path);
}

const char* dtb_get_property(const dtb_node_t* root,
                             const char* node_path,
                             const char* prop_name)
{
    kassert(node_path != NULL);
    kassert(prop_name != NULL);
    const dtb_node_t* node = dtb_find_node(root, node_path);
    if (!node) return NULL;

    const dtb_property_t* prop = find_prop(node, prop_name);
    return prop ? (const char*)prop->value : NULL;
}

uint64_t dtb_get_reg(const dtb_node_t* root, const char* path)
{
    kassert(path != NULL);
    const dtb_node_t* node = dtb_find_node(root, path);
    if (!node) return 0;

    const dtb_property_t* prop = find_prop(node, "reg");
    if (!prop) return 0;

    if (prop->length < 8)     // require 2 × BE32 cells
        return 0;

    uint32_t hi, lo;
    memcpy(&hi, prop->value, 4);
    memcpy(&lo, (uint8_t*)prop->value + 4, 4);

    hi = __builtin_bswap32(hi);
    lo = __builtin_bswap32(lo);

    return ((uint64_t)hi << 32) | lo;
}

static bool decode_reg32(const dtb_property_t* prop, uint32_t* addr_out, uint32_t* size_out) {
    kassert(prop != NULL);
    kassert(addr_out != NULL);
    kassert(size_out != NULL);

    const uint8_t* val = prop->value;
    uint32_t addr = 0;
    uint32_t size = 0;

    // Common layouts: [ADDR_HI][ADDR_LO][SIZE_HI][SIZE_LO] (16)
    //                 [ADDR_HI][ADDR_LO][SIZE] (12)
    //                 [ADDR][SIZE] (8)
    if (prop->length >= 16) {
        addr = read_be32(val + 4);
        size = read_be32(val + 12);
    } else if (prop->length >= 12) {
        addr = read_be32(val + 4);
        size = read_be32(val + 8);
    } else if (prop->length >= 8) {
        addr = read_be32(val + 0);
        size = read_be32(val + 4);
    } else {
        return false;
    }

    *addr_out = addr;
    *size_out = size;
    return true;
}

uint32_t dtb_get_reg_addr(const dtb_node_t* root, const char* path)
{
    kassert(path != NULL);
    const dtb_node_t* node = dtb_find_node(root, path);
    if (!node) return 0;

    const dtb_property_t* prop = find_prop(node, "reg");
    if (!prop) return 0;

    uint32_t addr, size;
    if (!decode_reg32(prop, &addr, &size)) return 0;
    (void)size; // size unused here
    return addr;
}

uint32_t dtb_get_reg_size(const dtb_node_t* root, const char* path)
{
    kassert(path != NULL);
    const dtb_node_t* node = dtb_find_node(root, path);
    if (!node) return 0;

    const dtb_property_t* prop = find_prop(node, "reg");
    if (!prop) return 0;

    uint32_t addr, size;
    if (!decode_reg32(prop, &addr, &size)) return 0;
    (void)addr; // addr unused here
    return size;
}

uint32_t dtb_get_ranges_parent_addr(const dtb_node_t* root, const char* path, uint32_t child_addr_hi)
{
    kassert(path != NULL);
    const dtb_node_t* node = dtb_find_node(root, path);
    if (!node) return 0;

    const dtb_property_t* ranges = find_prop(node, "ranges");
    if (!ranges) return 0;

    const uint8_t* val = ranges->value;
    size_t len = ranges->length;

    // For this platform: child #address-cells = 2, parent #address-cells = 2, size-cells = 1
    const size_t entry_bytes = 5 * sizeof(uint32_t); // hi, lo, parent_hi, parent_lo, size
    if (entry_bytes == 0 || len < entry_bytes) return 0;

    for (size_t offset = 0; offset + entry_bytes <= len; offset += entry_bytes) {
        uint32_t child_hi = read_be32(val + offset + 0);
        // uint32_t child_lo = read_be32(val + offset + 4);
        uint32_t parent_lo = read_be32(val + offset + 12);
        if (child_hi == child_addr_hi) {
            return parent_lo;
        }
    }
    return 0;
}
