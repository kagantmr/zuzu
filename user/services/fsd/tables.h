#ifndef FSD_TABLES_H
#define FSD_TABLES_H

#include <fs/protocols/fsd.h>
#include "client_table.h"

void TablesInit(const fs_backend_t *b, void *ctx);

/**
 * Allocates a session for the granted page and maps it. On success *badge is
 * the marker the client must present on every later request. Takes ownership
 * of `shm` only on ZUZU_OK.
 */
Err ClientRegister(Handle shm, Marker *badge);

/**
 * Resolves a marker to a live session, or NULL if the marker is unmarked,
 * out of range, free, or from an earlier generation of the slot.
 */
FsdClient *ClientFind(Marker badge);

uint32_t ClientSlot(const FsdClient *c);

/**
 * Closes the session's open files, unmaps and closes its page, and frees the
 * slot.
 */
void ClientDrop(FsdClient *c);

Err FileOpen(uint32_t slot, const char *path, uint32_t mode, uint32_t *fd_out);

/**
 * Backend file for (slot, fd), or NULL if the session does not own that fd.
 */
void *FileGet(uint32_t slot, uint32_t fd);

Err FileClose(uint32_t slot, uint32_t fd);

#endif /* FSD_TABLES_H */
