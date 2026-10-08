#ifndef PL111DRV_H
#define PL111DRV_H

#include <stdint.h>
#include <types.h>

typedef volatile uint32_t MMIORegister;

typedef struct __attribute__((packed)) {
    MMIORegister timing[4];
    MMIORegister upbase;
    MMIORegister lpbase;
    MMIORegister control;
    MMIORegister imsc;
    MMIORegister ris;
    MMIORegister mis;
    MMIORegister icr;
    MMIORegister upcurr;
    MMIORegister lpcurr;
} Pl111Mmio;

#endif