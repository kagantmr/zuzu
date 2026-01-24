#include "kernel/dtb/dtb.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib/mem.h"
#include "lib/string.h"
#include "core/assert.h"
#include "core/log.h"

typedef enum FDT_CODE
{
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE = 2,
    FDT_PROP = 3,
    FDT_NOP = 4,
    FDT_END = 9
} FDT_CODE;

typedef struct
{
    const uint8_t *dt_base;
    const uint8_t *str_base;
    const uint8_t *dtb_end;
} dtb_data_t;

static dtb_data_t g_dtb;
static bool g_dtb_ready;
static const void *g_dtb_base_ptr;

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
    if (dtb_base == NULL)
    {
        KERROR("Invalid DTB base pointer: Pointer %p is null", dtb_base);
        return false;
    }

    // If DTB is already initialized, allow re-entry.
    // - If the caller passes the same DTB base again, treat it as success.
    // - If the caller passes a different base, reset and re-initialize.
    if (g_dtb_ready)
    {
        if (g_dtb_base_ptr == dtb_base)
        {
            return true;
        }

        // Re-init with a different DTB base
        g_dtb_ready = false;
        g_dtb.dt_base = NULL;
        g_dtb.str_base = NULL;
        g_dtb.dtb_end = NULL;
        g_dtb_base_ptr = NULL;
    }

    const uint8_t *base = (const uint8_t *)dtb_base;

    // read the header
    const uint32_t magic = read_be32(dtb_base);
    const uint32_t totalsize = read_be32(base + 0x04);
    const uint32_t off_dt_struct = read_be32(base + 0x08);
    const uint32_t off_dt_strings = read_be32(base + 0x0C);
    if (magic != 0xD00DFEED)
    {
        KERROR("Could not validate DTB: Magic is %x, not 0xD00DFEED", magic);
        return false;
    }

    if (totalsize >= 0x28 && totalsize < 0x10000 && off_dt_struct < totalsize && off_dt_strings < totalsize &&
        base + off_dt_struct < base + totalsize && base + off_dt_strings < base + totalsize)
    {
        g_dtb.dtb_end = (const uint8_t *)base + totalsize;
        g_dtb.dt_base = (const uint8_t *)base + off_dt_struct;
        g_dtb.str_base = (const uint8_t *)base + off_dt_strings;
        g_dtb_base_ptr = dtb_base;
        g_dtb_ready = true;
        return true;
    }
    return false;
}

static bool read_token(const uint8_t **cur, uint32_t *out_token)
{
    kassert(cur != NULL);
    kassert(out_token != NULL);
    kassert(*cur != NULL);
    kassert(g_dtb_ready);

    // Need 4 bytes available to read a u32 token
    if (*cur + 4 > g_dtb.dtb_end)
    {
        return false;
    }

    *out_token = read_be32(*cur);
    *cur += 4;
    return true;
}

static bool skip_node_name(const uint8_t **cur, const char **out_name)
{
    kassert(cur != NULL);
    kassert(out_name != NULL);
    kassert(*cur != NULL);
    kassert(g_dtb_ready);

    const uint8_t *p = *cur;

    while (p < g_dtb.dtb_end && *p != '\0')
    {
        p++;
    }
    if (p >= g_dtb.dtb_end)
    {
        return false;
    }

    *out_name = (const char *)*cur;

    p++;

    uintptr_t aligned = ((uintptr_t)p + 3u) & ~((uintptr_t)3u);
    p = (const uint8_t *)aligned;

    if (p > g_dtb.dtb_end)
    {
        return false;
    }

    *cur = p;
    return true;
}

static bool read_property(const uint8_t **cur,
                          uint32_t *out_len,
                          const char **out_name,
                          const void **out_val)
{
    kassert(cur != NULL);
    kassert(*cur != NULL);
    kassert(out_len != NULL);
    kassert(out_name != NULL);
    kassert(out_val != NULL);
    kassert(g_dtb_ready);

    const uint8_t *p = *cur;

    // Need at least 8 bytes for (len, nameoff)
    if (p + 8 > g_dtb.dtb_end)
    {
        return false;
    }

    const uint32_t len = read_be32(p);
    const uint32_t nameoff = read_be32(p + 4);
    p += 8;

    // Value must fit inside DTB
    if (p + len > g_dtb.dtb_end)
    {
        return false;
    }

    // Resolve property name in strings block: str_base + nameoff
    const uint8_t *name_ptr = g_dtb.str_base + nameoff;
    if (name_ptr >= g_dtb.dtb_end)
    {
        return false;
    }

    // Optional robustness: ensure name is NUL-terminated before dtb_end
    const uint8_t *q = name_ptr;
    while (q < g_dtb.dtb_end && *q != '\0')
    {
        q++;
    }
    if (q >= g_dtb.dtb_end)
    {
        return false;
    }

    *out_len = len;
    *out_name = (const char *)name_ptr;
    *out_val = (const void *)p;

    // Advance past value bytes
    p += len;

    // Align to 4 bytes
    uintptr_t aligned = ((uintptr_t)p + 3u) & ~((uintptr_t)3u);
    p = (const uint8_t *)aligned;

    if (p > g_dtb.dtb_end)
    {
        return false;
    }

    *cur = p;
    return true;
}

#if defined(DTB_DEBUG_WALK)

#ifndef DTB_LOG
#define DTB_LOG kprintf
#endif

static void dtb_print_indent(int depth)
{
    for (int i = 0; i < depth; i++)
    {
        // Keep this lightweight in early boot
        kprintf("  ");
    }
}

void dtb_walk(void)
{
    kassert(g_dtb_ready);

    const uint8_t *cur = g_dtb.dt_base;
    uint32_t tok = 0;
    int depth = 0;

    while (true)
    {
        if (!read_token(&cur, &tok))
        {
            KERROR("DTB walk: failed to read token (out of bounds)");
            return;
        }

        switch (tok)
        {
        case FDT_BEGIN_NODE:
        {
            const char *name = NULL;
            if (!skip_node_name(&cur, &name))
            {
                KERROR("DTB walk: failed to read node name");
                return;
            }

            // Root node name is typically empty string
            dtb_print_indent(depth);
            if (name[0] == '\0')
            {
                DTB_LOG("/ (root)\n");
            }
            else
            {
                DTB_LOG("%s\n", name);
            }

            depth++;
            break;
        }

        case FDT_END_NODE:
            depth--;
            if (depth < 0)
            {
                KERROR("DTB walk: depth underflow (malformed DTB)");
                return;
            }
            dtb_print_indent(depth);
            DTB_LOG("}\n"); // visual marker
            break;

        case FDT_PROP:
        {
            uint32_t len = 0;
            const char *pname = NULL;
            const void *pval = NULL;

            if (!read_property(&cur, &len, &pname, &pval))
            {
                KERROR("DTB walk: failed to read property");
                return;
            }

            dtb_print_indent(depth);
            DTB_LOG("%s (len=%u)\n", pname, (unsigned)len);
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            if (depth != 0)
            {
                KERROR("DTB walk: reached FDT_END with depth=%d (malformed DTB)", depth);
            }
            else
            {
                DTB_LOG("DTB walk: done\n");
            }
            return;

        default:
            KERROR("DTB walk: unknown token 0x%08x", tok);
            return;
        }
    }
}

#endif

// Path segment parsing helper: returns next segment [seg, seg_len] and next_pos.
// `pos` is an index into `s` where scanning begins.
static inline bool path_peek_segment(const char *s, int pos,
                                     const char **seg, int *seg_len, int *next_pos)
{
    if (!s || !seg || !seg_len || !next_pos)
        return false;

    // Skip leading slashes
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

// Segment matches DTB node name: exact match OR prefix match up to '@'.
// If the segment contains '@', only exact match is allowed.
static inline bool segment_matches_node(const char *seg, int seg_len, const char *node_name)
{
    if (!seg || !node_name || seg_len <= 0)
        return false;

    // If segment contains '@', require exact match only.
    bool seg_has_at = false;
    for (int i = 0; i < seg_len; i++)
    {
        if (seg[i] == '@')
        {
            seg_has_at = true;
            break;
        }
    }

    // Compute node name lengths
    int full_len = 0;
    int at_len = -1;
    for (const char *p = node_name; *p != '\0'; p++)
    {
        if (*p == '@' && at_len < 0)
        {
            at_len = full_len;
        }
        full_len++;
    }

    // Exact match
    if (seg_len == full_len && memcmp(seg, node_name, (size_t)seg_len) == 0)
    {
        return true;
    }

    if (seg_has_at)
    {
        return false;
    }

    // Prefix-before-@ match
    if (at_len >= 0 && seg_len == at_len && memcmp(seg, node_name, (size_t)seg_len) == 0)
    {
        return true;
    }

    return false;
}

bool dtb_get_property(const char *path, const char *prop, const void **out_value, uint32_t *out_len)
{
    // Validate inputs, and ensure DTB is ready
    if (path == NULL || prop == NULL || out_value == NULL || out_len == NULL || !g_dtb_ready)
    {
        KERROR("dtb_get_property: invalid arguments or DTB not initialized");
        return false;
    }

    *out_value = NULL;
    *out_len = 0;

    // Root path special-case
    const bool want_root = (strcmp(path, "/") == 0);

    // Path segment parsing (no allocations)
    // path_pos always points to the position where the NEXT segment scan should begin.
    // (It may point at '/', the helpers will skip them.)
    int path_pos = 0;

    // DTB traversal state
    const uint8_t *cur = g_dtb.dt_base;
    uint32_t tok = 0;
    int depth = 0; // parent depth before entering a BEGIN_NODE

    // Path matching state
    int matched_segments = 0;
    bool in_target_node = false;
    int target_depth = -1; // depth of the target node (node depth, not parent depth)
    bool entered_root = false;

    // Small software stack to restore matching state when exiting subtrees
    enum
    {
        DTB_MAX_DEPTH = 64
    };
    int saved_matched[DTB_MAX_DEPTH];
    int saved_path_pos[DTB_MAX_DEPTH];

    // Initialize stacks
    for (int i = 0; i < DTB_MAX_DEPTH; i++)
    {
        saved_matched[i] = 0;
        saved_path_pos[i] = 0;
    }

    while (true)
    {
        if (!read_token(&cur, &tok))
        {
            return false;
        }

        switch (tok)
        {
        case FDT_BEGIN_NODE:
        {
            const char *name = NULL;

            // Save current matching state at this parent depth
            if (depth < 0 || depth >= DTB_MAX_DEPTH)
            {
                return false;
            }
            saved_matched[depth] = matched_segments;
            saved_path_pos[depth] = path_pos;

            if (!skip_node_name(&cur, &name))
            {
                return false;
            }

            const int node_depth = depth + 1;

            // Handle root node specially (empty name)
            if (name[0] == '\0')
            {
                entered_root = true;
                if (want_root)
                {
                    // Caller asked for "/" - this is the target
                    in_target_node = true;
                    target_depth = node_depth;
                }
                // Either way, enter root without matching any segment
                depth = node_depth;
                break;
            }

            // If we already found target, we just descend; property matching will be gated by depth.
            if (in_target_node)
            {
                depth = node_depth;
                break;
            }

            // Only attempt to match path segments after we have entered the DTB root.
            const int expected_depth = matched_segments + 2;
            if (entered_root && node_depth == expected_depth)
            {
                const char *seg = NULL;
                int seg_len = 0;
                int next_pos = 0;

                if (path_peek_segment(path, path_pos, &seg, &seg_len, &next_pos))
                {
                    if (segment_matches_node(seg, seg_len, name))
                    {
                        matched_segments++;
                        path_pos = next_pos;

                        // If no more segments, this is the target node.
                        const char *seg2 = NULL;
                        int seg2_len = 0;
                        int next_pos2 = 0;
                        if (!path_peek_segment(path, path_pos, &seg2, &seg2_len, &next_pos2))
                        {
                            in_target_node = true;
                            target_depth = node_depth;
                        }
                    }
                }
            }

            depth = node_depth;
            break;
        }
        case FDT_PROP:
        {
            uint32_t len = 0;
            const char *pname = NULL;
            const void *pval = NULL;

            if (!read_property(&cur, &len, &pname, &pval))
            {
                return false;
            }

            // Only consider properties that are directly inside the target node.
            if (in_target_node && depth == target_depth)
            {
                if (strcmp(pname, prop) == 0)
                {
                    *out_value = pval;
                    *out_len = len;
                    return true;
                }
            }
            break;
        }

        case FDT_END_NODE:
        {
            // Leaving current node
            depth--;
            if (depth < 0)
            {
                return false;
            }

            // If we just exited the target node without finding the property
            if (in_target_node && depth < target_depth)
            {
                return false;
            }

            // If we're still searching, restore match state to the parent's saved state
            if (!in_target_node)
            {
                if (depth < 0 || depth >= DTB_MAX_DEPTH)
                {
                    return false;
                }
                matched_segments = saved_matched[depth];
                path_pos = saved_path_pos[depth];
            }
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return false;

        default:
            return false;
        }
    }
}

bool dtb_get_u32(const char *path, const char *prop, uint32_t *out)
{
    if (path == NULL || prop == NULL || out == NULL || !g_dtb_ready)
    {
        return false;
    }

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, prop, &val, &len))
    {
        return false;
    }

    // Accept at least one cell; some properties may include more than one.
    if (len < 4)
    {
        return false;
    }

    *out = read_be32(val);
    return true;
}

bool dtb_get_reg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (path == NULL || out_addr == NULL || out_size == NULL || index < 0 || !g_dtb_ready)
    {
        return false;
    }

    // Fetch reg bytes
    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, "reg", &val, &len))
    {
        return false;
    }
    if (val == NULL || len == 0)
    {
        return false;
    }

    // Compute parent path (needed for #address-cells/#size-cells)
    char parent[256];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(parent))
    {
        return false;
    }

    // Copy path into parent buffer
    memcpy(parent, path, n + 1);

    // Strip trailing slashes (except keep single "/")
    while (n > 1 && parent[n - 1] == '/')
    {
        parent[n - 1] = '\0';
        n--;
    }

    // Find last '/' to get parent directory
    char *last = NULL;
    for (size_t i = 0; i < n; i++)
    {
        if (parent[i] == '/')
            last = &parent[i];
    }

    if (last == NULL)
    {
        // Shouldn't happen for absolute paths, but treat as root
        strcpy(parent, "/");
    }
    else if (last == parent)
    {
        // Parent of "/foo" is "/"
        parent[1] = '\0';
    }
    else
    {
        *last = '\0';
    }

    // Read cell sizes from parent, with conservative defaults if missing
    uint32_t addr_cells = 2;
    uint32_t size_cells = 1;
    (void)dtb_get_u32(parent, "#address-cells", &addr_cells);
    (void)dtb_get_u32(parent, "#size-cells", &size_cells);

    if (addr_cells == 0 || size_cells == 0 || addr_cells > 2 || size_cells > 2)
    {
        return false;
    }

    const uint32_t cells_per_entry = addr_cells + size_cells;
    const uint32_t bytes_per_entry = cells_per_entry * 4u;

    const uint32_t off = (uint32_t)index * bytes_per_entry;
    if (off + bytes_per_entry > len)
    {
        return false;
    }

    const uint8_t *p = (const uint8_t *)val + off;

    // Decode address
    uint64_t addr = 0;
    for (uint32_t i = 0; i < addr_cells; i++)
    {
        addr = (addr << 32) | (uint64_t)read_be32(p + i * 4u);
    }

    // Decode size
    uint64_t size = 0;
    const uint8_t *s = p + addr_cells * 4u;
    for (uint32_t i = 0; i < size_cells; i++)
    {
        size = (size << 32) | (uint64_t)read_be32(s + i * 4u);
    }

    *out_addr = addr;
    *out_size = size;
    return true;
}

bool dtb_find_compatible(const char *compatible, char *out_path, size_t out_path_cap)
{
    if (compatible == NULL || out_path == NULL || out_path_cap == 0 || !g_dtb_ready)
    {
        return false;
    }
    const uint8_t *cur = g_dtb.dt_base;
    uint32_t tok = 0;
    int depth = 0;
    size_t path_len = 0;
    out_path[0] = '\0';

    while (true)
    {
        if (!read_token(&cur, &tok))
        {
            return false;
        }

        switch (tok)
        {
        case FDT_BEGIN_NODE:
        {
            const char *name = NULL;
            if (!skip_node_name(&cur, &name))
            {
                return false;
            }

            // Append to path, handling root specially (root node name is empty)
            if (name[0] == '\0')
            {
                out_path[0] = '/';
                out_path[1] = '\0';
                path_len = 1;
            }
            else
            {
                const size_t name_len = strlen(name);

                // Determine whether we are currently at root ("/")
                const bool at_root = (path_len == 1 && out_path[0] == '/');
                const size_t extra_slash = at_root ? 0 : 1;

                if (path_len == 0)
                {
                    // Normalize empty to root prefix
                    out_path[0] = '/';
                    out_path[1] = '\0';
                    path_len = 1;
                }

                if (path_len + extra_slash + name_len + 1 > out_path_cap)
                {
                    return false;
                }

                if (!at_root)
                {
                    out_path[path_len++] = '/';
                }

                memcpy(out_path + path_len, name, name_len);
                path_len += name_len;
                out_path[path_len] = '\0';
            }

            depth++;
            break;
        }

        case FDT_END_NODE:
            depth--;
            if (depth < 0)
            {
                return false;
            }
            // Remove last path segment safely (avoid size_t underflow)
            if (path_len <= 1)
            {
                // Stay at root
                out_path[0] = '/';
                out_path[1] = '\0';
                path_len = 1;
                break;
            }

            size_t i = path_len - 1;
            while (i > 0 && out_path[i] != '/')
            {
                i--;
            }

            if (i == 0)
            {
                // Should not happen; normalize to root
                out_path[0] = '/';
                out_path[1] = '\0';
                path_len = 1;
            }
            else
            {
                // Truncate at the slash that starts this segment
                if (i == 0)
                {
                    out_path[0] = '/';
                    out_path[1] = '\0';
                    path_len = 1;
                }
                else
                {
                    out_path[i] = '\0';
                    path_len = i;
                    if (path_len == 0)
                    {
                        out_path[0] = '/';
                        out_path[1] = '\0';
                        path_len = 1;
                    }
                }
            }
            break;

        case FDT_PROP:
        {
            uint32_t len = 0;
            const char *pname = NULL;
            const void *pval = NULL;

            if (!read_property(&cur, &len, &pname, &pval))
            {
                return false;
            }

            if (strcmp(pname, "compatible") == 0)
            {
                // Check if compatible string is present
                const char *pstr = (const char *)pval;
                size_t remaining = len;
                while (remaining > 0)
                {
                    size_t slen = strnlen(pstr, remaining);
                    if (slen == remaining)
                    {
                        break; // No NUL found
                    }
                    if (strcmp(pstr, compatible) == 0)
                    {
                        return true; // Found
                    }
                    size_t advance = slen + 1;
                    pstr += advance;
                    remaining -= advance;
                }
            }
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return false;

        default:

            return false;
        }
    }
}

bool dtb_get_string(const char *path, const char *prop, char *out, size_t out_cap)
{
    if (path == NULL || prop == NULL || out == NULL || out_cap == 0 || !g_dtb_ready)
    {
        return false;
    }

    out[0] = '\0';

    const void *val = NULL;
    uint32_t len = 0;
    if (!dtb_get_property(path, prop, &val, &len))
    {
        return false;
    }

    if (val == NULL || len == 0)
    {
        return false;
    }

    const char *p = (const char *)val;

    // Ensure there is a NUL terminator within the DTB-reported length.
    size_t slen = 0;
    while (slen < (size_t)len && p[slen] != '\0')
    {
        slen++;
    }
    if (slen >= (size_t)len)
    {
        // Not a valid DTB string property
        return false;
    }

    // Copy (truncate if necessary) and always NUL-terminate.
    size_t to_copy = slen;
    if (to_copy >= out_cap)
    {
        to_copy = out_cap - 1;
    }

    if (to_copy > 0)
    {
        memcpy(out, p, to_copy);
    }
    out[to_copy] = '\0';

    return true;
}


// Helper: get parent path from a node path
static bool get_parent_path(const char *path, char *parent, size_t parent_cap)
{
    if (!path || !parent || parent_cap == 0)
        return false;

    size_t len = strlen(path);
    if (len == 0 || (len == 1 && path[0] == '/'))
    {
        // Root has no parent
        return false;
    }

    if (len >= parent_cap)
        return false;
    memcpy(parent, path, len + 1);

    // Strip trailing slashes
    while (len > 1 && parent[len - 1] == '/')
    {
        parent[--len] = '\0';
    }

    // Find last slash
    char *last_slash = NULL;
    for (size_t i = 0; i < len; i++)
    {
        if (parent[i] == '/')
            last_slash = &parent[i];
    }

    if (!last_slash || last_slash == parent)
    {
        // Parent is root
        parent[0] = '/';
        parent[1] = '\0';
    }
    else
    {
        *last_slash = '\0';
    }

    return true;
}

// Apply ranges translation from child address space to parent address space
static bool apply_ranges(const char *node_path, uint64_t child_addr, uint64_t *out_parent_addr)
{
    if (!node_path || !out_parent_addr)
        return false;

    const void *ranges_val = NULL;
    uint32_t ranges_len = 0;

    if (!dtb_get_property(node_path, "ranges", &ranges_val, &ranges_len))
    {
        // No ranges property - assume identity mapping
        *out_parent_addr = child_addr;
        return true;
    }

    // Empty ranges = 1:1 identity mapping
    if (ranges_len == 0)
    {
        *out_parent_addr = child_addr;
        return true;
    }

    // Get cell sizes from this node
    uint32_t child_addr_cells = 2;
    uint32_t child_size_cells = 1;
    (void)dtb_get_u32(node_path, "#address-cells", &child_addr_cells);
    (void)dtb_get_u32(node_path, "#size-cells", &child_size_cells);

    // Get parent's #address-cells
    char parent_path[256];
    uint32_t parent_addr_cells = 2;
    if (get_parent_path(node_path, parent_path, sizeof(parent_path)))
    {
        (void)dtb_get_u32(parent_path, "#address-cells", &parent_addr_cells);
    }

    if (child_addr_cells == 0 || child_addr_cells > 2 ||
        parent_addr_cells == 0 || parent_addr_cells > 2 ||
        child_size_cells == 0 || child_size_cells > 2)
    {
        return false;
    }

    uint32_t cells_per_entry = child_addr_cells + parent_addr_cells + child_size_cells;
    uint32_t bytes_per_entry = cells_per_entry * 4;

    if (bytes_per_entry == 0 || ranges_len % bytes_per_entry != 0)
    {
        return false;
    }

    const uint8_t *p = (const uint8_t *)ranges_val;
    uint32_t num_entries = ranges_len / bytes_per_entry;

    for (uint32_t i = 0; i < num_entries; i++)
    {
        const uint8_t *entry = p + (i * bytes_per_entry);

        // Read child bus address
        uint64_t child_base = 0;
        for (uint32_t j = 0; j < child_addr_cells; j++)
        {
            child_base = (child_base << 32) | read_be32(entry + j * 4);
        }

        // Read parent bus address
        uint64_t parent_base = 0;
        const uint8_t *parent_ptr = entry + child_addr_cells * 4;
        for (uint32_t j = 0; j < parent_addr_cells; j++)
        {
            parent_base = (parent_base << 32) | read_be32(parent_ptr + j * 4);
        }

        // Read size
        uint64_t range_size = 0;
        const uint8_t *size_ptr = parent_ptr + parent_addr_cells * 4;
        for (uint32_t j = 0; j < child_size_cells; j++)
        {
            range_size = (range_size << 32) | read_be32(size_ptr + j * 4);
        }

        // Check if child_addr falls within this range
        if (child_addr >= child_base && child_addr < child_base + range_size)
        {
            *out_parent_addr = parent_base + (child_addr - child_base);
            return true;
        }
    }

    // Address not in any range - try identity (some DTBs are sloppy)
    *out_parent_addr = child_addr;
    return true;
}

bool dtb_translate_address(const char *node_path, uint64_t raw_addr, uint64_t *out_phys)
{
    if (!node_path || !out_phys || !g_dtb_ready)
        return false;

    char current_path[256];
    size_t len = strlen(node_path);
    if (len >= sizeof(current_path))
        return false;
    memcpy(current_path, node_path, len + 1);

    uint64_t addr = raw_addr;

    // Walk up the tree, applying ranges at each level
    while (true)
    {
        char parent_path[256];
        if (!get_parent_path(current_path, parent_path, sizeof(parent_path)))
        {
            // Reached root
            break;
        }

        uint64_t translated;
        if (!apply_ranges(parent_path, addr, &translated))
        {
            return false;
        }

        addr = translated;

        // Move up
        len = strlen(parent_path);
        memcpy(current_path, parent_path, len + 1);

        if (len == 1 && parent_path[0] == '/')
        {
            break;
        }
    }

    *out_phys = addr;
    return true;
}

bool dtb_get_reg_phys(const char *path, int index, uint64_t *out_addr, uint64_t *out_size)
{
    if (!path || !out_addr || !out_size)
        return false;

    uint64_t raw_addr, size;
    if (!dtb_get_reg(path, index, &raw_addr, &size))
        return false;

    uint64_t phys_addr;
    if (!dtb_translate_address(path, raw_addr, &phys_addr))
        return false;

    *out_addr = phys_addr;
    *out_size = size;
    return true;
}