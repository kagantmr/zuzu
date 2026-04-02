#ifndef ZUZUSYSD_DEN_H
#define ZUZUSYSD_DEN_H

#include <stdint.h>
#include <vector.h>

#include "consts.h"

DEFINE_VEC(pid, uint32_t)      // pid_vec_t

// ------------------ Dens ------------------

typedef struct {
    uint32_t   id;
    uint32_t   owner_pid;
    char       name[SYSD_NAME_LEN];
    pid_vec_t  members;       // was uint32_t members[N]
    uint32_t    active;
} den_t;

void den_init(uint32_t sysd_pid);
int den_create(uint32_t owner_pid, uint32_t name_u32);
den_t *den_find(uint32_t den_id);
int den_has_member(uint32_t den_id, uint32_t pid);
int den_add_member(uint32_t den_id, uint32_t pid);
int den_remove_member(uint32_t den_id, uint32_t pid);
int den_is_owner(uint32_t den_id, uint32_t pid);
uint32_t den_first_for_pid(uint32_t pid);

#endif