#ifndef USER_DRIVERS_PL050DRV_PS2DEC_H
#define USER_DRIVERS_PL050DRV_PS2DEC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t usage;
    bool pressed;
} Ps2KeyEvent;

typedef enum {
    PS2_IDLE,
    PS2_EXT,
    PS2_EXT_BREAK,
    PS2_BREAK,
    PS2_PAUSE
} Ps2State;

typedef struct {
    Ps2State state;
    uint8_t skip;
} Ps2Decoder;

void Ps2DecoderInit(Ps2Decoder *d);
bool Ps2DecoderFeed(Ps2Decoder *d, uint8_t byte, Ps2KeyEvent *ev);

#endif /* USER_DRIVERS_PL050DRV_PS2DEC_H */
