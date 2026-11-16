#ifndef ZZSH_H
#define ZZSH_H

#include <fs/fsd_client.h>
#include <stdbool.h>

#define ZZSH_VER "v2.0"

#define LINE_BUFFER_SIZE 256
#define HISTORY_MAX 32

bool command_dispatch(const char *line);

/** @brief Fetches @p file from the TFTP server at @p server into the absolute path @p dest. */
int TftpGet(FsdConn *fsd, const char *server, const char *file, const char *dest);

#endif // ZZSH_H
