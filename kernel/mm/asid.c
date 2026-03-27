#include "asid.h"
#include "core/panic.h"

static uint8_t asid_bitmap[32];  // 256 bit

uint8_t asid_alloc(void) {
    for (int i = 1; i < 256; i++) {
        if (!(asid_bitmap[i/8] & (1 << (i%8)))) {
            asid_bitmap[i/8] |= (1 << (i%8));
            return (uint8_t)i;
        }
    }
    panic("ASID exhausted");
}

void asid_free(uint8_t asid) {
    if (asid == 0) return;
    asid_bitmap[asid/8] &= ~(1 << (asid%8));
}