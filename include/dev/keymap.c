#include <dev/hid.h>
#include <dev/keymap.h>
#include <string.h>

#define MODS_SHIFT (KEYMOD_LSHIFT | KEYMOD_RSHIFT)
#define MODS_CTRL (KEYMOD_LCTRL | KEYMOD_RCTRL)
#define MODS_ALT (KEYMOD_LALT | KEYMOD_RALT)
#define ESC 0x1B

static const char k_plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\r\x1b\x7f\t -=[]\\#;'`,./";
static const char k_shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1b\x7f\t _+{}|~:\"~<>?";

_Static_assert(sizeof(k_plain) - 1 == HID_KEY_SLASH - HID_KEY_A + 1, "plain table covers 0x04..0x38");
_Static_assert(sizeof(k_shifted) == sizeof(k_plain), "shifted table matches");

typedef struct {
    uint8_t usage;
    const char *seq;
} SpecialKey;

static const SpecialKey k_special[] = {
    {HID_KEY_UP, "\x1b[A"},      {HID_KEY_DOWN, "\x1b[B"},     {HID_KEY_RIGHT, "\x1b[C"},
    {HID_KEY_LEFT, "\x1b[D"},    {HID_KEY_HOME, "\x1b[H"},     {HID_KEY_END, "\x1b[F"},
    {HID_KEY_INSERT, "\x1b[2~"}, {HID_KEY_DELETE, "\x1b[3~"},  {HID_KEY_PAGEUP, "\x1b[5~"},
    {HID_KEY_PAGEDOWN, "\x1b[6~"}, {HID_KEY_F1, "\x1bOP"},     {HID_KEY_F2, "\x1bOQ"},
    {HID_KEY_F3, "\x1bOR"},      {HID_KEY_F4, "\x1bOS"},       {HID_KEY_F5, "\x1b[15~"},
    {HID_KEY_F6, "\x1b[17~"},    {HID_KEY_F7, "\x1b[18~"},     {HID_KEY_F8, "\x1b[19~"},
    {HID_KEY_F9, "\x1b[20~"},    {HID_KEY_F10, "\x1b[21~"},    {HID_KEY_F11, "\x1b[23~"},
    {HID_KEY_F12, "\x1b[24~"},
};

void KeymapInit(KeymapState *s)
{
    s->mods = 0;
    s->caps = false;
    s->num = true;
}

static const char *SpecialSeq(uint8_t usage)
{
    for (size_t i = 0; i < sizeof(k_special) / sizeof(k_special[0]); i++) {
        if (k_special[i].usage == usage)
            return k_special[i].seq;
    }
    return NULL;
}

static uint8_t KeypadNav(uint8_t usage)
{
    switch (usage) {
    case HID_KEY_KP_1: return HID_KEY_END;
    case HID_KEY_KP_2: return HID_KEY_DOWN;
    case HID_KEY_KP_3: return HID_KEY_PAGEDOWN;
    case HID_KEY_KP_4: return HID_KEY_LEFT;
    case HID_KEY_KP_6: return HID_KEY_RIGHT;
    case HID_KEY_KP_7: return HID_KEY_HOME;
    case HID_KEY_KP_8: return HID_KEY_UP;
    case HID_KEY_KP_9: return HID_KEY_PAGEUP;
    case HID_KEY_KP_0: return HID_KEY_INSERT;
    case HID_KEY_KP_PERIOD: return HID_KEY_DELETE;
    default: return HID_KEY_NONE;
    }
}

static char KeypadChar(uint8_t usage)
{
    if (usage >= HID_KEY_KP_1 && usage <= HID_KEY_KP_9)
        return (char)('1' + (usage - HID_KEY_KP_1));
    switch (usage) {
    case HID_KEY_KP_0: return '0';
    case HID_KEY_KP_PERIOD: return '.';
    case HID_KEY_KP_SLASH: return '/';
    case HID_KEY_KP_ASTERISK: return '*';
    case HID_KEY_KP_MINUS: return '-';
    case HID_KEY_KP_PLUS: return '+';
    case HID_KEY_KP_ENTER: return '\r';
    default: return 0;
    }
}

static char ApplyCtrl(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c & 0x1F);
    switch (c) {
    case '[': return 0x1B;
    case '\\': return 0x1C;
    case ']': return 0x1D;
    case ' ': return 0x00;
    default: return c;
    }
}

size_t KeymapFeed(KeymapState *s, uint8_t usage, bool pressed, uint8_t *out, size_t cap)
{
    if (usage >= HID_KEY_LCTRL && usage <= HID_KEY_RGUI) {
        uint8_t bit = (uint8_t)(1u << (usage - HID_KEY_LCTRL));
        if (pressed)
            s->mods |= bit;
        else
            s->mods &= (uint8_t)~bit;
        return 0;
    }
    if (!pressed)
        return 0;
    if (usage == HID_KEY_CAPSLOCK) {
        s->caps = !s->caps;
        return 0;
    }
    if (usage == HID_KEY_NUMLOCK) {
        s->num = !s->num;
        return 0;
    }

    uint8_t seq[KEYMAP_MAX_SEQ];
    size_t n = 0;
    bool shift = (s->mods & MODS_SHIFT) != 0;
    bool ctrl = (s->mods & MODS_CTRL) != 0;
    bool alt = (s->mods & MODS_ALT) != 0;

    char ch = 0;
    if (usage >= HID_KEY_KP_1 && usage <= HID_KEY_KP_PERIOD) {
        if (s->num)
            ch = KeypadChar(usage);
        else
            usage = KeypadNav(usage);
    } else if (usage >= HID_KEY_KP_SLASH && usage <= HID_KEY_KP_ENTER) {
        ch = KeypadChar(usage);
    } else if (usage >= HID_KEY_A && usage <= HID_KEY_SLASH) {
        bool letter = usage <= HID_KEY_Z;
        bool upper = letter ? (shift != s->caps) : shift;
        ch = (upper ? k_shifted : k_plain)[usage - HID_KEY_A];
        if (ctrl)
            ch = ApplyCtrl(k_plain[usage - HID_KEY_A]);
    }

    if (ch != 0 || (usage >= HID_KEY_A && usage <= HID_KEY_SLASH && ctrl)) {
        seq[n++] = (uint8_t)ch;
    } else {
        const char *sp = SpecialSeq(usage);
        if (!sp)
            return 0;
        for (; *sp && n < sizeof(seq); sp++)
            seq[n++] = (uint8_t)*sp;
    }

    size_t total = n + (alt ? 1U : 0U);
    if (total > cap)
        return 0;
    size_t o = 0;
    if (alt)
        out[o++] = ESC;
    memcpy(out + o, seq, n);
    return total;
}
