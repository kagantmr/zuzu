#ifndef FBCONS_ANSI_H
#define FBCONS_ANSI_H

#include <stdbool.h>
#include <stdint.h>

#define ANSI_MAX_PARAMS 8

typedef struct {
    void (*print)(void *ctx, uint8_t ch);
    void (*control)(void *ctx, uint8_t ch);
    void (*csi)(void *ctx, char prefix, char final, const uint32_t *params, uint32_t count);
} AnsiHandlers;

typedef enum { ANSI_GROUND, ANSI_ESC, ANSI_CSI, ANSI_OSC } AnsiState;

typedef struct {
    const AnsiHandlers *handlers;
    void *ctx;
    AnsiState state;
    uint32_t params[ANSI_MAX_PARAMS];
    uint32_t count;
    uint32_t cur;
    bool have_cur;
    char prefix;
} Ansi;

void AnsiInit(Ansi *a, const AnsiHandlers *handlers, void *ctx);
void AnsiFeed(Ansi *a, uint8_t b);

#endif /* FBCONS_ANSI_H */
