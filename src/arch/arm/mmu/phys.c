#include "kernel/mm/phys.h"

/* QEMU vexpress-a15:
   RAM: 0x80000000 → 0x88000000 (128 MB)
*/
phys_region_t phys_region = {
    .start = 0x80000000,
    .end   = 0x88000000,
};