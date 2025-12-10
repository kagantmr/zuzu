#ifndef PHYS_H
#define PHYS_H

#include <stdint.h>

typedef struct {
    uintptr_t start;   // physical start address of RAM region
    uintptr_t end;     // physical end address (exclusive)
} phys_region_t;

#endif