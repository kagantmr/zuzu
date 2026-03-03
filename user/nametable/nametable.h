#ifndef NAMETABLE_H
#define NAMETABLE_H

#include <stdint.h>

#define NAMETABLE_VERSION "v1.0"

// -------------------  Constraints  -------------------
#define NT_MAX_SERVICES  64
#define NT_NAME_LEN       4

/* Registry entry */
typedef struct {
    char     name[NT_NAME_LEN];
    uint32_t  handle;   /* slot in OUR handle table */
} nt_entry_t;



#endif