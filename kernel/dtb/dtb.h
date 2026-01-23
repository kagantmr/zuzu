#ifndef KERNEL_DTB_DTB_H
#define KERNEL_DTB_DTB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// In-place DTB parser interface

/**
 * Initializes the DTB parser with the given DTB base address.
 * @param dtb_base Pointer to the base of the DTB in memory.
 * @return true on success, false on failure.
 */
bool dtb_init(const void* dtb_base);

/**
 * Generic DTB accessor
 * @param path The path to the node in the DTB.
 * @param index The index of the memory region.
 * @param out_addr Pointer to store the starting address of the region.
 * @param out_size Pointer to store the size of the region.
 * @return true on success, false on failure.
 */
bool dtb_get_property(const char* path, const char* prop, const void** out_value, uint32_t* out_len);

/**
 * Retrieves a 32-bit unsigned integer property from the DTB.
 * @param path The path to the node in the DTB.
 * @param prop The name of the property.
 * @param out Pointer to store the retrieved value.
 * @return true on success, false on failure.
 */
bool dtb_get_u32(const char* path, const char* prop, uint32_t* out);

/**
 * Retrieves a 64-bit unsigned integer property from the DTB.
 * @param path The path to the node in the DTB.
 * @param prop The name of the property.
 * @param out Pointer to store the retrieved value.
 * @return true on success, false on failure.
 */
bool dtb_get_reg(const char* path, int index, uint64_t* out_addr, uint64_t* out_size);

/**
 * Finds a device node compatible with the given string.
 * @param compatible The compatible string to search for.
 * @param out_path Buffer to store the path of the found node.
 * @param out_path_cap Capacity of the output buffer.
 * @return true if a compatible node was found, false otherwise.
 */
bool dtb_find_compatible(const char* compatible, char* out_path, size_t out_path_cap);


#ifdef DTB_DEBUG_WALK
/**
 * Debug function to walk and print the DTB structure.
 */
void dtb_walk(void);
#endif

bool dtb_get_string(const char *path, const char *prop, char *out, size_t out_cap);

/**
 * Translates a raw address through parent ranges up to root.
 */
bool dtb_translate_address(const char *node_path, uint64_t raw_addr, uint64_t *out_phys);

/**
 * Get reg property with full address translation to physical.
 */
bool dtb_get_reg_phys(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);
#endif