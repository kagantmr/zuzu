#ifndef KERNEL_DTB_DTB_H
#define KERNEL_DTB_DTB_H

#include <stdint.h>

typedef struct dtb_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
} dtb_header_t;

typedef struct dtb_property {
    const char* name;
    const void* value;
    uint32_t length;
} dtb_property_t;

#define DTB_MAX_PROPS     32
#define DTB_MAX_CHILDREN  32

typedef struct dtb_node {
    const char* name;

    dtb_property_t properties[DTB_MAX_PROPS];
    uint32_t       property_count;

    struct dtb_node* children[DTB_MAX_CHILDREN];
    uint32_t        child_count;
} dtb_node_t;

#endif