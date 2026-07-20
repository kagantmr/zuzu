#ifndef FSD_CLIENT_TABLE_H
#define FSD_CLIENT_TABLE_H

#include <zuzu/types.h>
#include "ff.h"

#define FSD_MAX_CLIENTS 32

typedef struct {
    uint32_t pid;          /* 0 = free */
    handle_t shm_handle;
    void    *buf;
    uint32_t shm_size;
} fsd_client_t;

#define FSD_MAX_FILES        64
#define FSD_MAX_FILES_PER_CLIENT 16

typedef struct {
    uint32_t pid;      /* 0 = free */
    uint32_t fd;       /* client-visible fd */
    FIL      fil;
} fsd_file_t;

#endif // FSD_CLIENT_TABLE_H