#ifndef ZUZUSYSD_H
#define ZUZUSYSD_H

#include <stdint.h>

#define ZUZUSYSD_VERSION "v1.0"

// -------------------  Constraints  -------------------
#define NT_MAX_SERVICES 64
#define NT_NAME_LEN 4

/* Registry entry */
typedef struct
{
    char name[NT_NAME_LEN];
    uint32_t handle; /* slot in OUR handle table */
    uint32_t pid;
} nt_entry_t;

int nt_setup(void);
void sysd_loop(void);

#endif