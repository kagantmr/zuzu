
#ifndef RESERVE_H
#define RESERVE_H

#include <stdint.h>

typedef struct {
    uintptr_t start;
    uintptr_t end;
} reserved_region_t;

#endif