#ifndef ZZSH_H
#define ZZSH_H

#define ZZSH_VER "v1.3"

#include <zuzu/zuzu.h>
#include "zuzu/protocols/uart_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/lmsg.h"

#define LINE_BUFFER_SIZE 256
#define HISTORY_MAX      32

void zprint(const char* s);
void command_dispatch(const char *line);
int setup(void);

#endif // ZZSH_H