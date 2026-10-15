#ifndef KERNEL_LOADER_INITRD_H
#define KERNEL_LOADER_INITRD_H

#include <stdbool.h>
#include <stddef.h>

void InitrdInit(const void *start, size_t size);

bool InitrdFind(const char *name, const void **data_out, size_t *size_out);

#endif // KERNEL_LOADER_INITRD_H
