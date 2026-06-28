#ifndef NETRAND_H
#define NETRAND_H


#include <zuzu/types.h>
#include "globals.h"

void netrand_init(void);
uint32_t netrand_u32(void);

#endif // NETRAND_H