#ifndef DTB_PARSER_H
#define DTB_PARSER_H

#include <kernel/dtb/dtb.h>
#include <stdbool.h>

/**
 * Parses a Device Tree Blob (DTB) and constructs an in-memory representation.
 * @param dtb_data Pointer to the raw DTB data.
 * @return Pointer to the root dtb_node_t representing the DTB structure.
 */
dtb_node_t* dtb_parse(const void* dtb_data);
/**
 * Retrieves the 'reg' property of a specified node in the DTB.
 * @param dtb Pointer to the raw DTB data.
 * @param path Path to the node whose 'reg' property is to be retrieved.
 * @return The value of the 'reg' property as a uint32_t.
 */
uint32_t dtb_get_reg(const void* dtb, const char* path);

/**
 * Retrieves a property value from a specified node in the DTB.
 * @param dtb Pointer to the raw DTB data.
 * @param node Name of the node.
 * @param prop Name of the property to retrieve.
 * @return Pointer to the property value as a C-string.
 */
const char* dtb_get_property(const void* dtb, const char* node, const char* prop);

/**
 * Retrieves the size from the 'reg' property of a specified node in the DTB.
 * @param root Pointer to the root dtb_node_t.
 * @param path Path to the node whose 'reg' size is to be retrieved.
 * @return The size from the 'reg' property as a uint32_t.
 *
 */
uint32_t dtb_get_reg_size(const dtb_node_t* root, const char* path);

/**
 * Retrieves the address from the 'reg' property of a specified node in the DTB.
 * @param root Pointer to the root dtb_node_t.
 * @param path Path to the node whose 'reg' address is to be retrieved.
 * @return The address from the 'reg' property as a uint32_t.
 */
uint32_t dtb_get_reg_addr(const dtb_node_t* root, const char* path);


/**
 * Finds a node in the DTB by its path.
 * @param root Pointer to the root dtb_node_t.
 * @param path Path to the desired node.
 * @return Pointer to the found dtb_node_t, or NULL if not found.
 */
const dtb_node_t* dtb_find_node(const dtb_node_t* root, const char* path);

#endif // DTB_PARSER_H