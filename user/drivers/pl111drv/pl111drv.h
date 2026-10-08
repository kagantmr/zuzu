#ifndef PL111DRV_H
#define PL111DRV_H

#include <stddef.h>
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
    MMIORegister reserved[(0xFE0 - 0x34) / sizeof(uint32_t)];
    MMIORegister periph_id[4];
    MMIORegister pcell_id[4];
} Pl111Mmio;

_Static_assert(offsetof(Pl111Mmio, upbase) == 0x10, "upbase offset");
_Static_assert(offsetof(Pl111Mmio, control) == 0x18, "control offset");
_Static_assert(offsetof(Pl111Mmio, lpcurr) == 0x30, "lpcurr offset");
_Static_assert(offsetof(Pl111Mmio, periph_id) == 0xFE0, "periph_id offset");
_Static_assert(sizeof(Pl111Mmio) == 0x1000, "register block is one page");

#endif
