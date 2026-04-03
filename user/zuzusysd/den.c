#include "den.h"
#include <zuzu/protocols/nt_protocol.h>

static den_t den_table[SYSD_MAX_DENS];
static uint32_t den_next_id = 1;  // 0 is reserved for global

static inline void name_u32_to_chars(uint32_t name_u32, char out[SYSD_NAME_LEN]) {
    // LE packing
    out[0] = (char)((name_u32 >> 0)  & 0xFF);
    out[1] = (char)((name_u32 >> 8)  & 0xFF);
    out[2] = (char)((name_u32 >> 16) & 0xFF);
    out[3] = (char)((name_u32 >> 24) & 0xFF);
}

// den.c
void den_init(uint32_t sysd_pid) {
    for (int i = 0; i < SYSD_MAX_DENS; i++)
        den_table[i].active = 0;

    den_table[0].id = 0;
    den_table[0].owner_pid = sysd_pid;
    den_table[0].active = 1;
    pid_vec_init(&den_table[0].members);
}

int den_create(uint32_t owner_pid, uint32_t name_u32) {
    for (int i = 0; i < SYSD_MAX_DENS; i++) {
        if (!den_table[i].active) {
            den_table[i].id = den_next_id++;
            den_table[i].owner_pid = owner_pid;
            name_u32_to_chars(name_u32, den_table[i].name);
            den_table[i].active = 1;
            pid_vec_init(&den_table[i].members);
            pid_vec_push(&den_table[i].members, &owner_pid);
            return (int)den_table[i].id;
        }
    }
    return DEN_FULL;
}

den_t *den_find(uint32_t den_id) {
    for (int i = 0; i < SYSD_MAX_DENS; i++) {
        if (den_table[i].active && den_table[i].id == den_id)
            return &den_table[i];
    }
    return NULL;
}

int den_has_member(uint32_t den_id, uint32_t pid) {
    den_t *d = den_find(den_id);
    if (!d) return 0;
    for (uint32_t i = 0; i < d->members.len; i++) {
        if (d->members.data[i] == pid) return 1;
    }
    return 0;
}

int den_add_member(uint32_t den_id, uint32_t pid) {
    den_t *d = den_find(den_id);
    if (!d) return DEN_FAIL;
    // already in?
    for (uint32_t i = 0; i < d->members.len; i++) {
        if (d->members.data[i] == pid) return DEN_OK;
    }
    if (!pid_vec_push(&d->members, &pid)) return DEN_FULL;
    return DEN_OK;
}

int den_remove_member(uint32_t den_id, uint32_t pid) {
    den_t *d = den_find(den_id);
    if (!d) return DEN_FAIL;
    for (uint32_t i = 0; i < d->members.len; i++) {
        if (d->members.data[i] == pid) {
            d->members.data[i] = d->members.data[d->members.len - 1];
            d->members.len--;
            return DEN_OK;
        }
    }
    return DEN_FAIL;
}

int den_is_owner(uint32_t den_id, uint32_t pid) {
    den_t *d = den_find(den_id);
    return d && d->owner_pid == pid;
}

uint32_t den_first_for_pid(uint32_t pid) {
    for (int i = 1; i < SYSD_MAX_DENS; i++) {
        if (den_table[i].active && den_has_member(den_table[i].id, pid))
            return den_table[i].id;
    }
    return 0;
}

uint32_t den_count_for_pid(uint32_t pid) {
    uint32_t count = 0;
    for (int i = 1; i < SYSD_MAX_DENS; i++) {
        if (den_table[i].active && den_has_member(den_table[i].id, pid)) {
            count++;
        }
    }
    return count;
}

void den_scrub_pid(uint32_t pid) {
    for (int i = 0; i < SYSD_MAX_DENS; i++) {
        den_t *d = &den_table[i];
        if (!d->active) {
            continue;
        }

        for (uint32_t m = 0; m < d->members.len; m++) {
            if (d->members.data[m] == pid) {
                d->members.data[m] = d->members.data[d->members.len - 1];
                d->members.len--;
                break;
            }
        }

        if (d->id != 0 && d->owner_pid == pid) {
            if (d->members.len == 0) {
                pid_vec_destroy(&d->members);
                d->id = 0;
                d->owner_pid = 0;
                d->active = 0;
                for (int j = 0; j < SYSD_NAME_LEN; j++) {
                    d->name[j] = 0;
                }
            } else {
                d->owner_pid = d->members.data[0];
            }
        }
    }
}