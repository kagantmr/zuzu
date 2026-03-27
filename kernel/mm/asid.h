#ifndef ASID_H
#define ASID_H

#include <stdint.h>

uint8_t asid_alloc(void);
void asid_free(uint8_t asid);

#endif