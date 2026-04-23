#ifndef ZUZUSYSD_EXEC_H
#define ZUZUSYSD_EXEC_H

#include <stddef.h>
#include <stdint.h>

int exec_load(uint32_t task_handle, const void *elf_data, size_t elf_size, const char *argbuf, size_t argbuf_len, uint32_t argc);

#endif