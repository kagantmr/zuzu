#ifndef SYSD_EXEC_H
#define SYSD_EXEC_H

#include <stddef.h>
#include <stdint.h>
#include <zuzu/protocols/exec.h>

/* Accepts either an ELF or a ZXF image, dispatching on magic. */
int exec_inject(uint32_t taskHandle, const void *data, size_t size,
                const char *argbuf, size_t argbuf_len, uint32_t argc,
                ExecReply *out);

#endif