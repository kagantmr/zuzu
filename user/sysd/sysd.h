#ifndef SYSD_H
#define SYSD_H

#include <stdint.h>

#include "consts.h"
// -------------------  Constraints  -------------------

/* Registry entry */
typedef struct
{
    char name[SYSD_NAME_LEN];
    uint32_t handle; /* slot in OUR handle table */
    uint32_t pid;
    uint32_t den_id; 
} nt_entry_t;

// ------------------------------------

int nt_setup(void);
void sysd_loop(void);

#endif