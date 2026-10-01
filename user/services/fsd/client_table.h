#ifndef FSD_CLIENT_TABLE_H
#define FSD_CLIENT_TABLE_H

#include <stdbool.h>
#include <types.h>
#include "backend/backend.h"

#define FSD_MAX_CLIENTS 32

typedef struct
{
    bool in_use;
    uint32_t gen; /* survives free so a stale badge never matches a reused slot */
    Handle shm_handle;
    void *buf;
    uint32_t shm_size;
} FsdClient;

#define FSD_MAX_FILES 64
#define FSD_MAX_FILES_PER_CLIENT 16

typedef struct
{
    bool in_use;
    uint32_t slot; /* owning FsdClient index */
    uint32_t fd;
    uint8_t backend_file[MAX_BACKEND_FILE_SIZE];
} FsdFile;

#endif // FSD_CLIENT_TABLE_H
