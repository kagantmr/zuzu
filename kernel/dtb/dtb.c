#include "kernel/dtb/dtb.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <mem.h>
#include <string.h>
#include <libfdt.h>
#include "core/log.h"
#include "core/panic.h"
#include "kernel/layout.h"

static const void *g_fdt;
static bool g_dtb_ready;
extern kernel_layout_t kernel_layout;
static char      s_model[64];
static char      s_cpu[64];


static inline uint32_t read_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) |
           ((uint32_t)b[3]);
}

bool dtb_init(const void *dtb_base)
{
    if (!dtb_base)
        return false;

    int rc = fdt_check_header(dtb_base);
    if (rc < 0) {
        KERROR("Invalid DTB: %s", fdt_strerror(rc));
        return false;
    }

    g_fdt = dtb_base;
    g_dtb_ready = true;
    return true;
}

static inline bool path_peek_segment(const char *s, int pos,
                                     const char **seg, int *seg_len, int *next_pos)
{
    if (!s || !seg || !seg_len || !next_pos)
        return false;

    while (s[pos] == '/')
        pos++;
    if (s[pos] == '\0')
        return false;

    const int start = pos;
    while (s[pos] != '\0' && s[pos] != '/')
        pos++;

    *seg = s + start;
    *seg_len = pos - start;
    *next_pos = pos;
    return true;
}

static inline bool segment_matches_node(const char *seg, int seg_len, const char *node_name)
{
    if (!seg || !node_name || seg_len <= 0)
        return false;

    bool seg_has_at = false;
    for (int i = 0; i < seg_len; i++) {
        if (seg[i] == '@') {
            seg_has_at = true;
            break;
        }
    }

    int full_len = 0;
    int at_len = -1;
    for (const char *p = node_name; *p != '\0'; p++) {
        if (*p == '@' && at_len < 0)
            at_len = full_len;
        full_len++;
    }

    if (seg_len == full_len && memcmp(seg, node_name, (size_t)seg_len) == 0)
        return true;

    if (seg_has_at)
        return false;

    return at_len >= 0 &&
           seg_len == at_len &&
           memcmp(seg, node_name, (size_t)seg_len) == 0;
}

static int dtb_path_offset(const char *path)
{
    if (!g_dtb_ready || !path || path[0] != '/')
        return -FDT_ERR_BADPATH;

    int off = fdt_path_offset(g_fdt, path);
    if (off >= 0)
        return off;

    if (path[1] == '\0')
        return 0;

    int parent = 0;
    int pos = 0;
    const char *seg = NULL;
    int seg_len = 0;
    int next_pos = 0;

    while (path_peek_segment(path, pos, &seg, &seg_len, &next_pos)) {
        int child;
        int match = -FDT_ERR_NOTFOUND;

        fdt_for_each_subnode(child, g_fdt, parent) {
            const char *name = fdt_get_name(g_fdt, child, NULL);
            if (segment_matches_node(seg, seg_len, name)) {
                match = child;
                break;
            }
        }

        if (match < 0)
            return match;

        parent = match;
        pos = next_pos;
    }

    return parent;
}

static bool dtb_get_property(const char *path, const char *prop,
                             const void **out_value, uint32_t *out_len)
{
    if (!g_dtb_ready || !path || !prop || !out_value || !out_len)
        return false;

    int node = dtb_path_offset(path);
    if (node < 0)
        return false;

    int len = 0;
    const void *val = fdt_getprop(g_fdt, node, prop, &len);
    if (!val || len < 0)
        return false;

    *out_value = val;
    *out_len = (uint32_t)len;
    return true;
}

static bool dtb_get_u32(const char *path, const char *prop, uint32_t *out)
{
    if (!path || !prop || !out || !g_dtb_ready)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, prop, &val, &len) || len < 4)
        return false;

    *out = read_be32(val);
    return true;
}

static bool get_parent_path(const char *path, char *parent, size_t parent_cap)
{
    if (!path || !parent || parent_cap == 0)
        return false;

    size_t len = strlen(path);
    if (len == 0 || (len == 1 && path[0] == '/'))
        return false;

    if (len >= parent_cap)
        return false;
    memcpy(parent, path, len + 1);

    while (len > 1 && parent[len - 1] == '/')
        parent[--len] = '\0';

    char *last_slash = strrchr(parent, '/');
    if (!last_slash || last_slash == parent) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        *last_slash = '\0';
    }

    return true;
}

bool dtb_get_reg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!path || !out_addr || !out_size || index < 0 || !g_dtb_ready)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, "reg", &val, &len) || !val || len == 0)
        return false;

    char parent[256];
    if (!get_parent_path(path, parent, sizeof(parent)))
        return false;

    uint32_t addr_cells = 2;
    uint32_t size_cells = 1;
    (void)dtb_get_u32(parent, "#address-cells", &addr_cells);
    (void)dtb_get_u32(parent, "#size-cells", &size_cells);

    if (addr_cells == 0 || size_cells == 0 || addr_cells > 2 || size_cells > 2)
        return false;

    const uint32_t cells_per_entry = addr_cells + size_cells;
    const uint32_t bytes_per_entry = cells_per_entry * 4u;
    const uint32_t off = (uint32_t)index * bytes_per_entry;
    if (bytes_per_entry == 0 || off + bytes_per_entry > len)
        return false;

    const uint8_t *p = (const uint8_t *)val + off;

    uint64_t addr = 0;
    for (uint32_t i = 0; i < addr_cells; i++)
        addr = (addr << 32) | (uint64_t)read_be32(p + i * 4u);

    uint64_t size = 0;
    const uint8_t *s = p + addr_cells * 4u;
    for (uint32_t i = 0; i < size_cells; i++)
        size = (size << 32) | (uint64_t)read_be32(s + i * 4u);

    *out_addr = addr;
    *out_size = size;
    return true;
}

bool dtb_find_compatible(const char *compatible, char *out_path, size_t out_path_cap)
{
    if (!compatible || !out_path || out_path_cap == 0 || !g_dtb_ready)
        return false;

    int off = fdt_node_offset_by_compatible(g_fdt, -1, compatible);
    if (off < 0)
        return false;

    return fdt_get_path(g_fdt, off, out_path, (int)out_path_cap) == 0;
}

static bool dtb_get_string(const char *path, const char *prop, char *out, size_t out_cap)
{
    if (!path || !prop || !out || out_cap == 0 || !g_dtb_ready)
        return false;

    out[0] = '\0';

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, prop, &val, &len) || !val || len == 0)
        return false;

    const char *p = (const char *)val;
    size_t slen = strnlen(p, len);
    if (slen >= (size_t)len)
        return false;

    size_t to_copy = slen;
    if (to_copy >= out_cap)
        to_copy = out_cap - 1;

    if (to_copy > 0)
        memcpy(out, p, to_copy);
    out[to_copy] = '\0';
    return true;
}

static bool apply_ranges(const char *node_path, uint64_t child_addr, uint64_t *out_parent_addr)
{
    if (!node_path || !out_parent_addr)
        return false;

    const void *ranges_val = NULL;
    uint32_t ranges_len = 0;

    if (!dtb_get_property(node_path, "ranges", &ranges_val, &ranges_len)) {
        *out_parent_addr = child_addr;
        return true;
    }

    if (ranges_len == 0) {
        *out_parent_addr = child_addr;
        return true;
    }

    uint32_t child_addr_cells = 2;
    uint32_t child_size_cells = 1;
    (void)dtb_get_u32(node_path, "#address-cells", &child_addr_cells);
    (void)dtb_get_u32(node_path, "#size-cells", &child_size_cells);

    char parent_path[256];
    uint32_t parent_addr_cells = 2;
    if (get_parent_path(node_path, parent_path, sizeof(parent_path)))
        (void)dtb_get_u32(parent_path, "#address-cells", &parent_addr_cells);

    if (child_addr_cells == 0 || child_addr_cells > 2 ||
        parent_addr_cells == 0 || parent_addr_cells > 2 ||
        child_size_cells == 0 || child_size_cells > 2)
        return false;

    uint32_t cells_per_entry = child_addr_cells + parent_addr_cells + child_size_cells;
    uint32_t bytes_per_entry = cells_per_entry * 4;

    if (bytes_per_entry == 0 || ranges_len % bytes_per_entry != 0)
        return false;

    const uint8_t *p = (const uint8_t *)ranges_val;
    uint32_t num_entries = ranges_len / bytes_per_entry;

    for (uint32_t i = 0; i < num_entries; i++) {
        const uint8_t *entry = p + (i * bytes_per_entry);

        uint64_t child_base = 0;
        for (uint32_t j = 0; j < child_addr_cells; j++)
            child_base = (child_base << 32) | read_be32(entry + j * 4);

        uint64_t parent_base = 0;
        const uint8_t *parent_ptr = entry + child_addr_cells * 4;
        for (uint32_t j = 0; j < parent_addr_cells; j++)
            parent_base = (parent_base << 32) | read_be32(parent_ptr + j * 4);

        uint64_t range_size = 0;
        const uint8_t *size_ptr = parent_ptr + parent_addr_cells * 4;
        for (uint32_t j = 0; j < child_size_cells; j++)
            range_size = (range_size << 32) | read_be32(size_ptr + j * 4);

        if (child_addr >= child_base && child_addr < child_base + range_size) {
            *out_parent_addr = parent_base + (child_addr - child_base);
            return true;
        }
    }

    *out_parent_addr = child_addr;
    return true;
}

static bool dtb_translate_address(const char *node_path, uint64_t raw_addr, uint64_t *out_phys)
{
    if (!node_path || !out_phys || !g_dtb_ready)
        return false;

    char current_path[256];
    size_t len = strlen(node_path);
    if (len >= sizeof(current_path))
        return false;
    memcpy(current_path, node_path, len + 1);

    uint64_t addr = raw_addr;

    while (true) {
        char parent_path[256];
        if (!get_parent_path(current_path, parent_path, sizeof(parent_path)))
            break;

        uint64_t translated;
        if (!apply_ranges(parent_path, addr, &translated))
            return false;

        addr = translated;

        len = strlen(parent_path);
        memcpy(current_path, parent_path, len + 1);

        if (len == 1 && parent_path[0] == '/')
            break;
    }

    *out_phys = addr;
    return true;
}

bool dtb_get_reg_phys(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!path || !out_addr || !out_size)
        return false;

    uint64_t raw_addr;
    uint64_t size;
    if (!dtb_get_reg(path, index, &raw_addr, &size))
        return false;

    uint64_t phys_addr;
    if (!dtb_translate_address_arch(path, raw_addr, &phys_addr) &&
        !dtb_translate_address(path, raw_addr, &phys_addr))
        return false;

    *out_addr = phys_addr;
    *out_size = size;
    return true;
}

static bool dtb_resolve_irq_via_interrupt_map(const char *path,
                                              uint32_t child_irq,
                                              uint32_t *out_irq_num,
                                              uint32_t *out_flags)
{
    char parent_path[128];
    size_t plen = strlen(path);
    if (plen == 0 || plen >= sizeof(parent_path))
        return false;
    memmove(parent_path, path, plen + 1);

    for (int depth = 0; depth < 8; depth++) {
        char *last_slash = strrchr(parent_path, '/');
        if (!last_slash || last_slash == parent_path)
            break;
        *last_slash = '\0';

        const void *map_val = NULL;
        uint32_t map_len = 0;
        if (!dtb_get_property(parent_path, "interrupt-map", &map_val, &map_len))
            continue;

        if (!map_val || map_len == 0 || (map_len % 28) != 0)
            return false;

        const uint8_t *p = (const uint8_t *)map_val;
        uint32_t n_entries = map_len / 28;
        for (uint32_t i = 0; i < n_entries; i++, p += 28) {
            if (read_be32(p + 8) != child_irq)
                continue;

            uint32_t gic_type = read_be32(p + 16);
            uint32_t gic_num = read_be32(p + 20);
            *out_flags = read_be32(p + 24);
            *out_irq_num = (gic_type == 0) ? (gic_num + 32) : (gic_num + 16);
            return true;
        }

        return false;
    }

    return false;
}

static bool dtb_get_irq(const char *path, int index, uint32_t *out_irq_num, uint32_t *out_flags)
{
    if (!path || !out_irq_num || !out_flags || index < 0)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, "interrupts", &val, &len) || !val || len == 0)
        return false;

    if ((len % 12) == 0) {
        uint32_t off = (uint32_t)index * 12;
        if (off + 12 > len)
            return false;

        const uint8_t *p = (const uint8_t *)val + off;
        uint32_t type = read_be32(p);
        uint32_t number = read_be32(p + 4);
        *out_flags = read_be32(p + 8);
        *out_irq_num = (type == 0) ? (number + 32) : (number + 16);
        return true;
    }

    if ((len % 4) == 0) {
        uint32_t count = len / 4;
        if ((uint32_t)index >= count)
            return false;
        uint32_t child_irq = read_be32((const uint8_t *)val + ((uint32_t)index * 4));
        if (dtb_resolve_irq_arch(path, child_irq, out_irq_num, out_flags))
            return true;
        return dtb_resolve_irq_via_interrupt_map(path, child_irq, out_irq_num, out_flags);
    }

    return false;
}

void dtb_enum_devices(void (*cb)(const char *compatible,
                                 const char *path,
                                 uint64_t phys, uint64_t size,
                                 uint32_t irq))
{
    if (!cb || !g_dtb_ready)
        return;

    int depth = -1;
    int off = -1;
    while ((off = fdt_next_node(g_fdt, off, &depth)) >= 0) {
        int compat_len = 0;
        const char *compat = fdt_getprop(g_fdt, off, "compatible", &compat_len);
        if (!compat || compat_len <= 0)
            continue;

        int reg_len = 0;
        const void *reg = fdt_getprop(g_fdt, off, "reg", &reg_len);
        if (!reg || reg_len <= 0)
            continue;

        /* use a small local copy of the first compatible string */
        char first_compat[64];
        size_t slen = strnlen(compat, (size_t)compat_len);
        if (slen == (size_t)compat_len)
            continue;
        size_t copy = slen < sizeof(first_compat) - 1 ? slen : sizeof(first_compat) - 1;
        memcpy(first_compat, compat, copy);
        first_compat[copy] = '\0';

        char path[256];
        if (fdt_get_path(g_fdt, off, path, sizeof(path)) < 0)
            continue;

        uint64_t phys = 0;
        uint64_t size = 0;
        if (!dtb_get_reg_phys(path, 0, &phys, &size))
            continue;

        uint32_t irq_num = 0;
        uint32_t irq_flags = 0;
        (void)irq_flags;
        dtb_get_irq(path, 0, &irq_num, &irq_flags);

        cb(first_compat, path, phys, size, irq_num);
    }
}

uint32_t dtb_count_devices(void)
{
    if (!g_dtb_ready)
        return 0;
    uint32_t cnt = 0;
    int depth = -1;
    int off = -1;
    while ((off = fdt_next_node(g_fdt, off, &depth)) >= 0) {
        int compat_len = 0;
        const char *compat = fdt_getprop(g_fdt, off, "compatible", &compat_len);
        if (!compat || compat_len <= 0)
            continue;
        int reg_len = 0;
        const void *reg = fdt_getprop(g_fdt, off, "reg", &reg_len);
        if (!reg || reg_len <= 0)
            continue;
        cnt++;
    }
    return cnt;
}

const char *dtb_model(void)
{
    if (!g_dtb_ready)
        return "Unknown";
    if (dtb_get_string("/", "model", s_model, sizeof(s_model)))
        return s_model;
    return "Unknown";
}

const char *dtb_cpu_compat(void)
{
    if (!g_dtb_ready)
        return "Unknown";
    if (dtb_get_string("/cpus/cpu@0", "compatible", s_cpu, sizeof(s_cpu)))
        return s_cpu;
    return "Unknown";
}

__attribute__((weak)) bool dtb_translate_address_arch(const char *node_path, uint64_t raw_addr, uint64_t *out_phys)
{
    (void)node_path; (void)raw_addr; (void)out_phys;
    return false;
}

__attribute__((weak)) bool dtb_resolve_irq_arch(const char *node_path, uint32_t child_irq, uint32_t *out_irq, uint32_t *out_flags)
{
    (void)node_path; (void)child_irq; (void)out_irq; (void)out_flags;
    return false;
}

void dtb_shutdown(void)
{
    g_fdt = NULL;
    g_dtb_ready = false;
}
