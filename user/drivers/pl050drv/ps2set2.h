#ifndef USER_DRIVERS_PL050DRV_PS2SET2_H
#define USER_DRIVERS_PL050DRV_PS2SET2_H

#include <stdbool.h>
#include <stdint.h>

uint8_t Ps2Set2ToHid(uint8_t code, bool extended);

#endif /* USER_DRIVERS_PL050DRV_PS2SET2_H */
