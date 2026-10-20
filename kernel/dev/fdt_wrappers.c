/**
 * @file fdt_wrappers.c
 * @brief Implementation of the device tree wrappers over libfdt.
 */

#include "fdt_wrappers.h"
#include "core/log.h"
#include <libfdt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief The device tree blob in use, or NULL before FdtInit(). */
static const void *g_fdt;
/** @brief Whether g_fdt points at a blob whose header was checked. */
static bool g_fdt_ready;
/** @brief Backing store for the string FdtModel() returns. */
static char s_model[64];
/** @brief Backing store for the string FdtCpuCompat() returns. */
static char s_cpu[64];

/**
 * @brief Read a big-endian 32-bit word from possibly unaligned memory.
 *
 * @param[in] p Pointer to the four bytes.
 * @return The value in host order.
 */
static inline uint32_t read_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) |
           ((uint32_t)b[3]);
}

bool FdtInit(const void *base)
{
    if (!base)
        return false;

    int rc = fdt_check_header(base);
    if (rc < 0) {
        KERROR("Invalid DTB: %s", fdt_strerror(rc));
        return false;
    }

    g_fdt = base;
    g_fdt_ready = true;
    return true;
}

/**
 * @brief Take the next component of a slash-separated path.
 *
 * @param[in] s The path.
 * @param[in] pos Offset to start from; leading slashes are skipped.
 * @param[out] seg Receives a pointer to the start of the component inside @p s.
 * @param[out] seg_len Receives the component's length.
 * @param[out] next_pos Receives the offset just past the component.
 * @return true if a component was found, false at the end of the path.
 */
static inline bool path_peek_segment(const char *s, int pos, const char **seg, int *seg_len,
                                     int *next_pos)
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

/**
 * @brief Compare a path component with a node name, letting a name without a unit address match.
 *
 * "uart" matches "uart@9000000" but "uart@1" does not match "uart@2".
 *
 * @param[in] seg The path component, not NUL-terminated.
 * @param[in] seg_len The component's length.
 * @param[in] node_name The node's full name.
 * @return true if they match.
 */
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

    return at_len >= 0 && seg_len == at_len && memcmp(seg, node_name, (size_t)seg_len) == 0;
}

/**
 * @brief Find a node by path, accepting components without a unit address.
 *
 * @param[in] path Absolute path of the node.
 * @return The node's offset in the blob, or a negative libfdt error.
 */
static int FdtPathOffset(const char *path)
{
    if (!g_fdt_ready || !path || path[0] != '/')
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

        fdt_for_each_subnode(child, g_fdt, parent)
        {
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

/**
 * @brief Look up a property of a node.
 *
 * @param[in] path Absolute path of the node.
 * @param[in] prop Name of the property.
 * @param[out] out_value Receives a pointer into the blob.
 * @param[out] out_len Receives the property's length in bytes.
 * @return true if the node and property exist.
 */
static bool FdtGetProperty(const char *path, const char *prop, const void **out_value,
                           uint32_t *out_len)
{
    if (!g_fdt_ready || !path || !prop || !out_value || !out_len)
        return false;

    int node = FdtPathOffset(path);
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

/**
 * @brief Read a property's first 32-bit cell.
 *
 * @param[in] path Absolute path of the node.
 * @param[in] prop Name of the property.
 * @param[out] out Receives the value.
 * @return true if the property exists and is at least four bytes long.
 */
static bool FdtGetU32(const char *path, const char *prop, uint32_t *out)
{
    if (!path || !prop || !out || !g_fdt_ready)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!FdtGetProperty(path, prop, &val, &len) || len < 4)
        return false;

    *out = read_be32(val);
    return true;
}

/**
 * @brief Compute the path of a node's parent.
 *
 * @param[in] path Absolute path of the node.
 * @param[out] parent Receives the parent's path; "/" for a child of the root.
 * @param[in] parent_cap Size of @p parent in bytes.
 * @return false for the root itself or if @p path does not fit.
 */
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

bool FdtGetReg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!path || !out_addr || !out_size || index < 0 || !g_fdt_ready)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!FdtGetProperty(path, "reg", &val, &len) || !val || len == 0)
        return false;

    char parent[256];
    if (!get_parent_path(path, parent, sizeof(parent)))
        return false;

    uint32_t addr_cells = 2;
    uint32_t size_cells = 1;
    (void)FdtGetU32(parent, "#address-cells", &addr_cells);
    (void)FdtGetU32(parent, "#size-cells", &size_cells);

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

size_t FdtTotalSize(void)
{
    if (!g_fdt_ready)
        return 0;
    return fdt_totalsize(g_fdt);
}

bool FdtGetReservedMem(uint32_t index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!out_addr || !out_size || !g_fdt_ready)
        return false;

    int n = fdt_num_mem_rsv(g_fdt);
    if (n < 0 || index >= (uint32_t)n)
        return false;

    return fdt_get_mem_rsv(g_fdt, (int)index, out_addr, out_size) == 0;
}

/**
 * @brief Read a one- or two-cell big-endian number.
 *
 * @param[in] val Pointer to the property value.
 * @param[in] len Length of the value, which must be 4 or 8.
 * @param[out] out Receives the number.
 * @return true if @p len is 4 or 8.
 */
static bool read_be_cells(const void *val, uint32_t len, uint64_t *out)
{
    if (len == 4) {
        *out = read_be32(val);
        return true;
    }
    if (len == 8) {
        *out = ((uint64_t)read_be32(val) << 32) | read_be32((const uint8_t *)val + 4);
        return true;
    }
    return false;
}

bool FdtGetInitrd(uint64_t *out_start, uint64_t *out_end)
{
    if (!out_start || !out_end || !g_fdt_ready)
        return false;

    const void *sval = NULL, *eval = NULL;
    uint32_t slen = 0, elen = 0;
    if (!FdtGetProperty("/chosen", "linux,initrd-start", &sval, &slen) ||
        !FdtGetProperty("/chosen", "linux,initrd-end", &eval, &elen))
        return false;

    uint64_t start, end;
    if (!read_be_cells(sval, slen, &start) || !read_be_cells(eval, elen, &end))
        return false;

    if (end <= start)
        return false;

    *out_start = start;
    *out_end = end;
    return true;
}

/**
 * @brief Copy a string property, truncating to the buffer.
 *
 * @param[in] path Absolute path of the node.
 * @param[in] prop Name of the property.
 * @param[out] out Receives the NUL-terminated string.
 * @param[in] out_cap Size of @p out in bytes.
 * @return true if the property exists and holds a terminated string.
 */
static bool FdtGetString(const char *path, const char *prop, char *out, size_t out_cap)
{
    if (!path || !prop || !out || out_cap == 0 || !g_fdt_ready)
        return false;

    out[0] = '\0';

    const void *val = NULL;
    uint32_t len = 0;
    if (!FdtGetProperty(path, prop, &val, &len) || !val || len == 0)
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

/**
 * @brief Translate an address through one node's @c ranges property.
 *
 * A node with no @c ranges, or an empty one, passes the address through unchanged, as does an
 * address no range covers.
 *
 * @param[in] node_path Path of the node whose ranges apply.
 * @param[in] child_addr The address in the node's own address space.
 * @param[out] out_parent_addr Receives the address in the parent's space.
 * @return false if the cell sizes are unsupported or @c ranges is malformed.
 */
static bool apply_ranges(const char *node_path, uint64_t child_addr, uint64_t *out_parent_addr)
{
    if (!node_path || !out_parent_addr)
        return false;

    const void *ranges_val = NULL;
    uint32_t ranges_len = 0;

    if (!FdtGetProperty(node_path, "ranges", &ranges_val, &ranges_len)) {
        *out_parent_addr = child_addr;
        return true;
    }

    if (ranges_len == 0) {
        *out_parent_addr = child_addr;
        return true;
    }

    uint32_t child_addr_cells = 2;
    uint32_t child_size_cells = 1;
    (void)FdtGetU32(node_path, "#address-cells", &child_addr_cells);
    (void)FdtGetU32(node_path, "#size-cells", &child_size_cells);

    char parent_path[256];
    uint32_t parent_addr_cells = 2;
    if (get_parent_path(node_path, parent_path, sizeof(parent_path)))
        (void)FdtGetU32(parent_path, "#address-cells", &parent_addr_cells);

    if (child_addr_cells == 0 || child_addr_cells > 2 || parent_addr_cells == 0 ||
        parent_addr_cells > 2 || child_size_cells == 0 || child_size_cells > 2)
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

/**
 * @brief Translate a node's address up to the root by applying each ancestor's @c ranges.
 *
 * @param[in] node_path Path of the node the address belongs to.
 * @param[in] raw_addr The address as written in the node.
 * @param[out] out_phys Receives the translated address.
 * @return true on success.
 */
static bool FdtTranslateAddress(const char *node_path, uint64_t raw_addr, uint64_t *out_phys)
{
    if (!node_path || !out_phys || !g_fdt_ready)
        return false;

    static char current_path[256];
    size_t len = strlen(node_path);
    if (len >= sizeof(current_path))
        return false;
    memcpy(current_path, node_path, len + 1);

    uint64_t addr = raw_addr;

    while (true) {
        
        if (!get_parent_path(current_path, current_path, sizeof(current_path)))
            break;

        uint64_t translated;
        if (!apply_ranges(current_path, addr, &translated))
            return false;

        addr = translated;

        if (current_path[0] == '/' && current_path[1] == '\0')
            break;
    }

    *out_phys = addr;
    return true;
}

bool FdtGetRegPa(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!path || !out_addr || !out_size)
        return false;

    uint64_t raw_addr;
    uint64_t size;
    if (!FdtGetReg(path, index, &raw_addr, &size))
        return false;

    uint64_t phys_addr;
    if (!FdtTranslateAddressArch(path, raw_addr, &phys_addr) &&
        !FdtTranslateAddress(path, raw_addr, &phys_addr))
        return false;

    *out_addr = phys_addr;
    *out_size = size;
    return true;
}

/**
 * @brief Resolve a single-cell interrupt through an ancestor's @c interrupt-map.
 *
 * @param[in] path Path of the node.
 * @param[in] child_irq The interrupt number as written in the node.
 * @param[out] out_irq_num Receives the GIC interrupt ID.
 * @param[out] out_flags Receives the interrupt flags.
 * @return true if a map entry matched.
 */
static bool FdtResolveIrqViaMap(const char *path, uint32_t child_irq, uint32_t *out_irq_num,
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
        if (!FdtGetProperty(parent_path, "interrupt-map", &map_val, &map_len))
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

/**
 * @brief Read one interrupt of a node as a GIC interrupt ID.
 *
 * Three-cell specifiers are decoded directly, with shared peripheral numbers offset by 32 and
 * private ones by 16. A single-cell specifier goes through FdtResolveIrqArch() and then the
 * interrupt map.
 *
 * @param[in] path Path of the node.
 * @param[in] index Which interrupt to read, from 0.
 * @param[out] out_irq_num Receives the GIC interrupt ID.
 * @param[out] out_flags Receives the interrupt flags.
 * @return true on success.
 */
static bool FdtGetIrq(const char *path, int index, uint32_t *out_irq_num, uint32_t *out_flags)
{
    if (!path || !out_irq_num || !out_flags || index < 0)
        return false;

    const void *val = NULL;
    uint32_t len = 0;
    if (!FdtGetProperty(path, "interrupts", &val, &len) || !val || len == 0)
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
        if (FdtResolveIrqArch(path, child_irq, out_irq_num, out_flags))
            return true;
        return FdtResolveIrqViaMap(path, child_irq, out_irq_num, out_flags);
    }

    return false;
}

void FdtEnumerateDevices(void (*cb)(const char *compatible, const char *path, uint64_t phys,
                                    uint64_t size, uint32_t irq))
{
    if (!cb || !g_fdt_ready)
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

        static char first_compat[64];
        size_t slen = strnlen(compat, (size_t)compat_len);
        if (slen == (size_t)compat_len)
            continue;
        size_t copy = slen < sizeof(first_compat) - 1 ? slen : sizeof(first_compat) - 1;
        memcpy(first_compat, compat, copy);
        first_compat[copy] = '\0';

        static char path[256];
        if (fdt_get_path(g_fdt, off, path, sizeof(path)) < 0)
            continue;

        uint64_t phys = 0;
        uint64_t size = 0;
        if (!FdtGetRegPa(path, 0, &phys, &size))
            continue;

        uint32_t irq_num = 0;
        uint32_t irq_flags = 0;
        (void)irq_flags;
        FdtGetIrq(path, 0, &irq_num, &irq_flags);

        cb(first_compat, path, phys, size, irq_num);
    }
}

size_t FdtDeviceCount(void)
{
    if (!g_fdt_ready)
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

const char *FdtModel(void)
{
    if (!g_fdt_ready)
        return "Unknown";
    if (FdtGetString("/", "model", s_model, sizeof(s_model)))
        return s_model;
    return "Unknown";
}

const char *FdtCpuCompat(void)
{
    if (!g_fdt_ready)
        return "Unknown";
    if (FdtGetString("/cpus/cpu@0", "compatible", s_cpu, sizeof(s_cpu)))
        return s_cpu;
    return "Unknown";
}

__attribute__((weak)) bool FdtTranslateAddressArch(const char *node_path, uint64_t raw_addr,
                                                   uint64_t *out_phys)
{
    (void)node_path;
    (void)raw_addr;
    (void)out_phys;
    return false;
}

__attribute__((weak)) bool FdtResolveIrqArch(const char *node_path, uint32_t child_irq,
                                             uint32_t *out_irq, uint32_t *out_flags)
{
    (void)node_path;
    (void)child_irq;
    (void)out_irq;
    (void)out_flags;
    return false;
}

void FdtShutdown(void)
{
    g_fdt = NULL;
    g_fdt_ready = false;
}
