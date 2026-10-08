#ifndef PL111DRV_H
#define PL111DRV_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

typedef volatile uint32_t MMIORegister;

typedef struct {
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

#define PL111_CTRL_EN (1U << 0)
#define PL111_CTRL_BPP_565 (6U << 1)
#define PL111_CTRL_TFT (1U << 5)
#define PL111_CTRL_BGR (1U << 8)
#define PL111_CTRL_PWR (1U << 11)

#define FB_WIDTH 640U
#define FB_HEIGHT 480U
#define FB_BYTES (FB_WIDTH * FB_HEIGHT * 2U)
#define FB_PAGES (FB_BYTES / 4096U)

_Static_assert(FB_BYTES % 4096U == 0, "framebuffer is a whole number of pages");
_Static_assert(offsetof(Pl111Mmio, upbase) == 0x10, "upbase offset");
_Static_assert(offsetof(Pl111Mmio, control) == 0x18, "control offset");
_Static_assert(offsetof(Pl111Mmio, lpcurr) == 0x30, "lpcurr offset");
_Static_assert(offsetof(Pl111Mmio, periph_id) == 0xFE0, "periph_id offset");
_Static_assert(sizeof(Pl111Mmio) == 0x1000, "register block is one page");

#endif
