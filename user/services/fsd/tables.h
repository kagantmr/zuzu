#ifndef FSD_TABLES_H
#define FSD_TABLES_H

#include <zuzu/protocols/fsd_protocol.h>
#include "client_table.h"


/**
 * Client and file tables for FSD service.
 */

void tables_init(const fs_backend_t *b, void *ctx);

 /**
  * Register a new client with the given PID and shared memory handle.
  */
err_t          client_register(uint32_t pid, handle_t shm, uint32_t size);

/**
 * Find a client by (pid). Returns NULL if not found.
 */
fsd_client_t  *client_find(uint32_t pid);

/**
 * Drop a client by (pid). Unmaps its shared memory and closes any open files.
 */
void           client_drop(uint32_t pid);   /* unmaps shm, closes its files */

/**
 * Open a file for the given client (pid) with the specified path and mode. Returns a file descriptor in fd_out.
 */
err_t  file_open(uint32_t pid, const char *path, uint32_t mode, uint32_t *fd_out);

/**
 * Get the backend file pointer for the given client (pid) and file descriptor (fd). Returns NULL if the file is not owned by the client.
 */
void  *file_get(uint32_t pid, uint32_t fd);   /* backend file ptr, NULL if not owned */

/**
 * Close a file for the given client (pid) and file descriptor (fd). Returns an error code if the operation fails.
 */
err_t  file_close(uint32_t pid, uint32_t fd);

#endif /* FSD_TABLES_H */