/**
 * @file fdt_wrappers.h
 * @brief Kernel access to the device tree the bootloader passed in.
 */

#ifndef KERNEL_DEV_FDT_WRAPPERS_H
#define KERNEL_DEV_FDT_WRAPPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief One device found in the device tree.
 */
typedef struct {
    char compatible[64]; /**< The first compatible string of the node, NUL-terminated. */
    uint64_t phys; /**< Physical base address of the first register block. */
    uint64_t size; /**< Size of the first register block in bytes. */
    uint64_t phys2; /**< Physical base address of the second register block, or 0 if none. */
    uint64_t size2; /**< Size of the second register block in bytes, or 0 if none. */
    uint32_t irq; /**< GIC interrupt ID, or 0 if the node has none. Shared peripheral numbers are
                       offset by 32 and private ones by 16. */
    uint32_t nregs; /**< Number of register blocks recorded: 1 or 2. */
} FdtDevice;

/**
 * @brief Point the wrappers at a device tree blob after checking its header.
 *
 * @param[in] base Address of the blob; it must stay mapped while the wrappers are in use.
 * @return true if the header is valid, false otherwise.
 */
bool FdtInit(const void *base);

/**
 * @brief Call @p cb for every node that has both a compatible string and a register block.
 *
 * Nodes whose address cannot be translated to a physical address are skipped. The strings passed
 * to the callback are only valid for the duration of the call.
 *
 * @param[in] cb Called with the first compatible string, the node path, the physical address and
 * size of the first register block, and the first interrupt (0 if none).
 */
void FdtEnumerateDevices(void (*cb)(const char *compatible, const char *path, uint64_t phys,
                                    uint64_t size, uint32_t irq));

/**
 * @brief Get the size of the device tree blob.
 *
 * @return The total size in bytes, or 0 if no blob has been set up.
 */
size_t FdtTotalSize(void);

/**
 * @brief Count the nodes FdtEnumerateDevices() would visit, ignoring address translation.
 *
 * @return The number of nodes with a compatible string and a register block.
 */
size_t FdtDeviceCount(void);

/**
 * @brief Read an entry of the blob's memory reservation table.
 *
 * @param[in] index Index of the entry.
 * @param[out] out_addr Receives the reserved address.
 * @param[out] out_size Receives the reserved size in bytes.
 * @return true on success, false if @p index is past the end of the table.
 */
bool FdtGetReservedMem(uint32_t index, uint64_t *out_addr, uint64_t *out_size);

/**
 * @brief Read a register block of a node exactly as written in its @c reg property.
 *
 * The address is not translated through the parent's @c ranges; use FdtGetRegPa() for that.
 *
 * @param[in] path Path of the node.
 * @param[in] index Which register block to read, from 0.
 * @param[out] out_addr Receives the address.
 * @param[out] out_size Receives the size in bytes.
 * @return true on success, false if the node or block does not exist or the cell sizes are
 * unsupported.
 */
bool FdtGetReg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

/**
 * @brief Get the bounds of the initrd the bootloader recorded under @c /chosen.
 *
 * @param[out] out_start Receives the start address.
 * @param[out] out_end Receives the end address, which is one past the last byte.
 * @return true if both properties exist and describe a non-empty range.
 */
bool FdtGetInitrd(uint64_t *out_start, uint64_t *out_end);

/**
 * @brief Read a register block and translate its address to a physical address.
 *
 * The architecture hook FdtTranslateAddressArch() is tried first, then the generic walk up the
 * parents' @c ranges properties.
 *
 * @param[in] path Path of the node.
 * @param[in] index Which register block to read, from 0.
 * @param[out] out_addr Receives the physical address.
 * @param[out] out_size Receives the size in bytes.
 * @return true on success, false if the block does not exist or cannot be translated.
 */
bool FdtGetRegPa(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

/**
 * @brief Get the board's model string from the root node.
 *
 * @return The string, valid until FdtShutdown(), or "Unknown".
 */
const char *FdtModel(void);

/**
 * @brief Get the compatible string of the first CPU node.
 *
 * @return The string, valid until FdtShutdown(), or "Unknown".
 */
const char *FdtCpuCompat(void);

/**
 * @brief Architecture hook that translates a node's address to a physical one.
 *
 * The default implementation does nothing and returns false, so the generic @c ranges walk is
 * used. An architecture overrides it by defining the symbol.
 *
 * @param[in] node_path Path of the node.
 * @param[in] raw_addr The address as written in the node.
 * @param[out] out_phys Receives the physical address.
 * @return true if the hook handled the translation.
 */
bool FdtTranslateAddressArch(const char *node_path, uint64_t raw_addr, uint64_t *out_phys);

/**
 * @brief Architecture hook that resolves a single-cell interrupt specifier to a GIC interrupt.
 *
 * The default implementation does nothing and returns false, so the node's @c interrupt-map is
 * used. An architecture overrides it by defining the symbol.
 *
 * @param[in] node_path Path of the node.
 * @param[in] child_irq The interrupt number as written in the node.
 * @param[out] out_irq Receives the GIC interrupt ID.
 * @param[out] out_flags Receives the interrupt flags from the specifier.
 * @return true if the hook handled the resolution.
 */
bool FdtResolveIrqArch(const char *node_path, uint32_t child_irq, uint32_t *out_irq,
                       uint32_t *out_flags);

/**
 * @brief Forget the blob. The wrappers return failure until FdtInit() is called again.
 */
void FdtShutdown(void);

#endif
