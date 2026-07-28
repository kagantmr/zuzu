#ifndef PL111DRV_H
#define PL111DRV_H

#include <stdint.h>
#include <zuzu/types.h>

typedef volatile uint32_t MMIORegister;

typedef struct __attribute__((packed)) {
    MMIORegister TIMING[4];
    MMIORegister UPBASE;
    MMIORegister LPBASE;
    MMIORegister CONTROL;
    MMIORegister IMSC;
    MMIORegister RIS;
    MMIORegister MIS;
    MMIORegister ICR;
    MMIORegister UPCURR;
    MMIORegister LPCURR;
} PL111;

#endif