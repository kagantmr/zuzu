/**
 * @file initrd.h
 * @brief Read access to the initial ramdisk cpio archive.
 */

#ifndef KERNEL_LOADER_INITRD_H
#define KERNEL_LOADER_INITRD_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Record where the initrd cpio archive is mapped.
 *
 * @param[in] start Kernel virtual address of the archive.
 * @param[in] size Size of the archive in bytes; must be larger than a cpio header.
 */
void InitrdInit(const void *start, size_t size);

/**
 * @brief Look up a file in the initrd.
 *
 * @param[in] name Path of the file in the archive.
 * @param[out] data_out Receives a pointer to the file contents.
 * @param[out] size_out Receives the file size in bytes.
 * @return true if the file was found, false otherwise.
 */
bool InitrdFind(const char *name, const void **data_out, size_t *size_out);

#endif // KERNEL_LOADER_INITRD_H
