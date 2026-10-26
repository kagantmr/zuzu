#include "ps2dec.h"
#include "ps2set2.h"

void Ps2DecoderInit(Ps2Decoder *d)
{
    d->state = PS2_IDLE;
    d->skip = 0;
}

static bool Emit(Ps2KeyEvent *ev, uint8_t usage, bool pressed)
{
    if (usage == 0)
        return false;
    ev->usage = usage;
    ev->pressed = pressed;
    return true;
}

bool Ps2DecoderFeed(Ps2Decoder *d, uint8_t byte, Ps2KeyEvent *ev)
{
    switch (d->state) {
    case PS2_IDLE:
        switch (byte) {
        case 0xE0: d->state = PS2_EXT; return false;
        case 0xF0: d->state = PS2_BREAK; return false;
        case 0xE1: d->state = PS2_PAUSE; d->skip = 7; return false;
        case 0xFA: case 0xFE: case 0xAA: return false;
        default:   return Emit(ev, Ps2Set2ToHid(byte, false), true);
        }
    case PS2_EXT:
        if (byte == 0xF0) { d->state = PS2_EXT_BREAK; return false; }
        d->state = PS2_IDLE;
        return Emit(ev, Ps2Set2ToHid(byte, true), true);    /* E0 12 and E0 7C map to nothing */
    case PS2_BREAK:
        d->state = PS2_IDLE;
        return Emit(ev, Ps2Set2ToHid(byte, false), false);
    case PS2_EXT_BREAK:
        d->state = PS2_IDLE;
        return Emit(ev, Ps2Set2ToHid(byte, true), false);
    case PS2_PAUSE:
        if (--d->skip == 0) d->state = PS2_IDLE;
        return false;
    }
    return false;
}