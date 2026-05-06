#ifndef KSYM_H
#define KSYM_H

#include <stdint.h>

typedef struct {
    uint32_t addr;
    const char *name;
} ksym_entry_t;

extern volatile const uint32_t ksym_count;
extern const ksym_entry_t ksym_table[];

const char *ksym_lookup(uint32_t addr);

#endif