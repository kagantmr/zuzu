#ifndef SYSD_EXEC_H
#define SYSD_EXEC_H

#include <stddef.h>
#include <stdint.h>
#include <zuzu/protocols/sysd_protocol.h>

int exec_inject(uint32_t task_handle, const void *elf_data, size_t elf_size,
                const char *argbuf, size_t argbuf_len, uint32_t argc,
                exec_reply_t *out);

#endif