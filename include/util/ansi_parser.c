/**
 * @file
 * @brief ANSI escape-sequence parser.
 */

#include <util/ansi_parser.h>

/** @brief Largest value a CSI parameter accumulates to. */
#define PARAM_CAP 100000U

void AnsiInit(Ansi *a, const AnsiHandlers *handlers, void *ctx)
{
    *a = (Ansi){.handlers = handlers, .ctx = ctx, .state = ANSI_GROUND};
}

/**
 * @brief Forgets the parameters of the sequence being parsed.
 *
 * @param[in,out] a Parser.
 */
static void CsiReset(Ansi *a)
{
    a->count = 0;
    a->cur = 0;
    a->have_cur = false;
    a->prefix = 0;
}

/**
 * @brief Ends the current CSI parameter and stores it if there is room.
 *
 * @param[in,out] a Parser.
 */
static void CsiPush(Ansi *a)
{
    if (a->count < ANSI_MAX_PARAMS)
        a->params[a->count++] = a->cur;
    a->cur = 0;
    a->have_cur = false;
}

void AnsiFeed(Ansi *a, uint8_t b)
{
    const AnsiHandlers *h = a->handlers;

    if (b == 0x18 || b == 0x1A) {
        a->state = ANSI_GROUND;
        return;
    }
    if (b == 0x1B) {
        a->state = ANSI_ESC;
        return;
    }

    switch (a->state) {
    case ANSI_GROUND:
        if (b < 0x20 || b == 0x7F)
            h->control(a->ctx, b);
        else
            h->print(a->ctx, b);
        return;
    case ANSI_ESC:
        if (b == '[') {
            CsiReset(a);
            a->state = ANSI_CSI;
        } else if (b == ']') {
            a->state = ANSI_OSC;
        } else {
            a->state = ANSI_GROUND;
        }
        return;
    case ANSI_OSC:
        if (b == 0x07)
            a->state = ANSI_GROUND;
        return;
    case ANSI_CSI:
        break;
    }

    if (b < 0x20) {
        h->control(a->ctx, b);
    } else if (b >= '0' && b <= '9') {
        if (a->cur < PARAM_CAP)
            a->cur = (a->cur * 10U) + (b - '0');
        a->have_cur = true;
    } else if (b == ';') {
        CsiPush(a);
    } else if (b >= 0x3C && b <= 0x3F) {
        if (a->count == 0 && !a->have_cur)
            a->prefix = (char)b;
    } else if (b >= 0x40 && b <= 0x7E) {
        if (a->have_cur || a->count > 0)
            CsiPush(a);
        h->csi(a->ctx, a->prefix, (char)b, a->params, a->count);
        a->state = ANSI_GROUND;
    }
}
