#ifndef ASID_H
#define ASID_H

#include <stdint.h>

typedef struct {
    uint8_t  asid;
    uint32_t generation;
} asid_token_t;

asid_token_t asid_alloc(void);
void asid_free(asid_token_t token);
uint32_t asid_current_generation(void);

#endif