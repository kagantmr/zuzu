#ifndef DTB_PARSER_H
#define DTB_PARSER_H

#include <dtb.h>
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
 * Retrieves the translated 'reg' property address and size for a given node path,
 * taking into account any address translations defined by 'ranges' properties
 * in the DTB hierarchy.
 * @param node Pointer to the current dtb_node_t being examined.
 * @param target_path Path to the target node whose 'reg' property is to be retrieved
 * after translation.
 * @param parent_base The accumulated base address translation from parent nodes.
 * @param parent_addr_cells The number of address cells defined by the parent node.
 * @param out_addr Pointer to store the translated address.
 * @param out_size Pointer to store the size from the 'reg' property.
 * @return true if the target node was found and values were retrieved; false otherwise.
 *
 */
#endif // DTB_PARSER_H