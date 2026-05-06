#ifndef KSYM_H
#define KSYM_H

#include <stdint.h>

typedef struct {
    uint32_t addr;
    const char *name;
} ksym_entry_t;

extern const ksym_entry_t ksym_table[];
extern const uint32_t ksym_count;

const char *ksym_lookup(uint32_t addr);

#endif