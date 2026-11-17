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
#define PL111_CTRL_VCOMP_SHIFT 12 /* 0 = start of vsync, 1 = back porch, 2 = active, 3 = front */

#define PL111_INT_FUF (1U << 1)
#define PL111_INT_LNBU (1U << 2)
#define PL111_INT_VCOMP (1U << 3)
#define PL111_INT_MBERROR (1U << 4)
#define PL111_INT_ALL (PL111_INT_FUF | PL111_INT_LNBU | PL111_INT_VCOMP | PL111_INT_MBERROR)

#define PL111_PART_ID 0x111U
#define PL111_PCELL_MAGIC 0xB105F00DU

#define PL111_PIXEL_ORDER PL111_CTRL_BGR
#define PL111_PCD 0U         /* pixel clock divider, ignored by the emulated panel */
#define PL111_POWER_DELAY_MS 20

typedef struct {
    uint32_t width; /* multiple of 16 */
    uint32_t height;
    uint32_t hfp, hsw, hbp; /* horizontal front porch, sync width, back porch in pixels */
    uint32_t vfp, vsw, vbp; /* same in lines */
} Pl111Mode;

#define PL111_BPP 16U
#define FB_MAX_BUFFERS 2U

static inline uint32_t Pl111Timing0(const Pl111Mode *m)
{
    return ((m->width / 16U - 1U) << 2) | ((m->hsw - 1U) << 8) | ((m->hfp - 1U) << 16) |
           ((m->hbp - 1U) << 24);
}

static inline uint32_t Pl111Timing1(const Pl111Mode *m)
{
    return (m->height - 1U) | ((m->vsw - 1U) << 10) | (m->vfp << 16) | (m->vbp << 24);
}

static inline uint32_t Pl111Timing2(const Pl111Mode *m)
{
    return PL111_PCD | ((m->width - 1U) << 16);
}

_Static_assert(offsetof(Pl111Mmio, upbase) == 0x10, "upbase offset");
_Static_assert(offsetof(Pl111Mmio, lpbase) == 0x14, "lpbase offset");
_Static_assert(offsetof(Pl111Mmio, control) == 0x18, "control offset");
_Static_assert(offsetof(Pl111Mmio, imsc) == 0x1C, "imsc offset");
_Static_assert(offsetof(Pl111Mmio, ris) == 0x20, "ris offset");
_Static_assert(offsetof(Pl111Mmio, mis) == 0x24, "mis offset");
_Static_assert(offsetof(Pl111Mmio, icr) == 0x28, "icr offset");
_Static_assert(offsetof(Pl111Mmio, upcurr) == 0x2C, "upcurr offset");
_Static_assert(offsetof(Pl111Mmio, lpcurr) == 0x30, "lpcurr offset");
_Static_assert(offsetof(Pl111Mmio, periph_id) == 0xFE0, "periph_id offset");
_Static_assert(offsetof(Pl111Mmio, pcell_id) == 0xFF0, "pcell_id offset");
_Static_assert(sizeof(Pl111Mmio) == 0x1000, "register block is one page");

#endif
