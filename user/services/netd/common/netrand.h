#ifndef NETRAND_H
#define NETRAND_H

#include "globals.h"
#include <types.h>

void netrand_init(void);
uint32_t netrand_u32(void);

#endif // NETRAND_H