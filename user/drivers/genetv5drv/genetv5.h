#ifndef USER_DRIVERS_GENETV5DRV_GENETV5DRV_H
#define USER_DRIVERS_GENETV5DRV_GENETV5DRV_H

#include <types.h>

typedef struct {
    volatile uint32_t rev_ctrl;          /* 0x00 */
    volatile uint32_t port_ctrl;         /* 0x04 */
    volatile uint32_t rbuf_flush_ctrl;   /* 0x08 */
    volatile uint32_t tbuf_flush_ctrl;   /* 0x0c */
} GenetSys;
_Static_assert(offsetof(GenetSys, tbuf_flush_ctrl) == 0x0c, "layout");

typedef struct {
    volatile uint32_t unused;
    volatile uint32_t hd_bkp_ctrl;
    volatile uint32_t cmd;
    volatile uint32_t mac0;
    volatile uint32_t mac1;
} GenetUmac;

#endif /* USER_DRIVERS_GENETV5DRV_GENETV5DRV_H */