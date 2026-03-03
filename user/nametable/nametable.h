#ifndef NAMETABLE_H
#define NAMETABLE_H

#include <stdint.h>

#define NAMETABLE_VERSION "v1.0"

// ------------------- IPC constants -------------------
#define NT_REGISTER  1
#define NT_LOOKUP    2

#define NT_LU_OK       0
#define NT_LU_NOMATCH (-1)
#define NT_REG_FAIL   (-2)
#define NT_REG_OK      0
#define NT_BADCMD     (-3)

// -------------------  Constraints  -------------------
#define NT_MAX_SERVICES  64
#define NT_NAME_LEN       4

/* Registry entry */
typedef struct {
    char     name[NT_NAME_LEN];
    uint32_t  handle;   /* slot in OUR handle table */
} nt_entry_t;

/* Well-known handle for the name server in every process */
#define NT_PORT  0

#endif