#ifndef ZZSH_H
#define ZZSH_H

#define ZZSH_VER "v1.0"

#include "zuzu.h"
#include "zuzu/protocols/zuart_protocol.h"
#include "zuzu/protocols/nt_protocol.h"

#define LINE_BUFFER_SIZE 256
#define HISTORY_MAX      32

void zprint(const char* s);
size_t zread(char *dst, size_t max);
void command_dispatch(const char *line);
int setup(void);

#endif // ZZSH_H