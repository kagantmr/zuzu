#ifndef ARCH_ASID_H
#define ARCH_ASID_H

#include <stdint.h>

typedef uint8_t Asid;

typedef struct {
    Asid asid;
    uint32_t generation;
} AsidToken;

AsidToken AsidAlloc(void);

void AsidFree(AsidToken token);

extern uint32_t asid_generation;
static inline uint32_t AsidCurrentGeneration(void) { return asid_generation; }

void AsidSetActive(Asid asid);

#endif // ARCH_ASID_H
