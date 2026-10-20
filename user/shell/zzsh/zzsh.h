#ifndef ZZSH_H
#define ZZSH_H

#include <stdbool.h>

#define ZZSH_VER "v2.0"

#define LINE_BUFFER_SIZE 256
#define HISTORY_MAX 32

bool command_dispatch(const char *line);

#endif // ZZSH_H
