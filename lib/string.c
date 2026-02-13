#include "lib/string.h"
#include <stdint.h>
#include <stdarg.h>
#include "lib/convert.h"
#include "core/assert.h"

size_t strlen(const char *s) {
    kassert(s != NULL);
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

size_t strnlen (const char *s, size_t maxlen)
{
  size_t len;
  for (len = 0; len < maxlen; ++len)
    if (s[len] == '\0')
      break;
  return len;
}

char *strcat(char *dest, const char *src) {
    kassert(dest != NULL && src != NULL);
    char *end = dest;

    while (*end) end++;

    strcpy(end, src);

    *end = '\0';

    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    kassert(dest != NULL && src != NULL);
    char *end = dest;

    while (*end) end++;  // find end of dest

    strncpy(end, src, n);     // now copy src starting at the end

    return dest;
}


int strcmp(const char *s1, const char *s2) {
    kassert(s1 != NULL && s2 != NULL);
    int diff;
    while (*s1 && *s2) {
        diff = *s1++ - *s2++;
        if (diff != 0) {
            return diff;
        };
    }

    return *s1 - *s2;
}


int strncmp(const char *s1, const char *s2, size_t n) {
    kassert(s1 != NULL && s2 != NULL);
    signed char diff;
    size_t compared = 0;
    while (*s1 && *s2 && compared < n) {
        diff = *s1++ - *s2++;
        if (diff != 0) {
            return diff;
        };
        compared++;
    }
    if (compared == n) return 0;
    return *s1 - *s2;
}

char *strcpy(char *dest, const char *src) {
    kassert(dest != NULL && src != NULL);
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    kassert(dest != NULL && src != NULL);
    char *ret = dest;

    while (n && (*dest++ = *src++)) {
        n--;
    }

    // If we stopped because src ended early,
    // pad with '\0' until n is zero.
    while (n--) {
        *dest++ = '\0';
    }

    return ret;
}

char *strchr(const char *s, int c) {
    kassert(s != NULL);
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return NULL;
}

void strfmt(void (*outc)(char), const char *fstring, ...) {
    kassert(outc != NULL);
    kassert(fstring != NULL);
    va_list args;
    

    va_start(args, fstring);
    vstrfmt(outc, fstring, args);
    va_end(args);
}

// --- helpers for vstrfmt field width and padding ---
static void emit_repeat(void (*outc)(char), char ch, int count) {
    while (count-- > 0) outc(ch);
}

static void emit_strn(void (*outc)(char), const char *s, int n) {
    for (int i = 0; i < n; i++) outc(s[i]);
}

static int is_digit(char c) { return (c >= '0' && c <= '9'); }

static int parse_int(const char **pp) {
    const char *p = *pp;
    int v = 0;
    while (is_digit(*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

// Convert unsigned value to string in given base. Returns length.
// Output is in forward order in buf (null-terminated).
static int utoa_ull(char *buf, unsigned long long v, unsigned base, int uppercase) {
    static const char digs_lo[] = "0123456789abcdef";
    static const char digs_hi[] = "0123456789ABCDEF";
    const char *digs = uppercase ? digs_hi : digs_lo;

    char tmp[65];
    int n = 0;

    if (base < 2) base = 10;

    // Fast paths for power-of-two bases (no division)
    if (base == 16) {
        if (v == 0) tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0xFULL)];
            v >>= 4;
        }
    } else if (base == 8) {
        if (v == 0) tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0x7ULL)];
            v >>= 3;
        }
    } else if (base == 2) {
        if (v == 0) tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0x1ULL)];
            v >>= 1;
        }
    } else if (base == 10) {
        // Long division by 10 without using 64-bit / or % (avoids __aeabi_uldivmod)
        if (v == 0) {
            tmp[n++] = '0';
        } else {
            while (v != 0) {
                unsigned long long q = 0;
                unsigned int r = 0;

                // Bitwise long division: (q, r) = v / 10, v % 10
                for (int i = 63; i >= 0; i--) {
                    r = (r << 1) | (unsigned int)((v >> i) & 1ULL);
                    if (r >= 10U) {
                        r -= 10U;
                        q |= (1ULL << i);
                    }
                }

                tmp[n++] = (char)('0' + r);
                v = q;
            }
        }
    } else {
        // Fallback: support only bases we explicitly handle in this kernel
        // (Add other bases here if needed.)
        tmp[n++] = '?';
    }

    // reverse into buf
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

typedef enum {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_Z
} length_t;

static unsigned long long get_unsigned_arg(va_list *args, length_t len) {
    switch (len) {
        case LEN_HH: return (unsigned char)va_arg(*args, unsigned int);
        case LEN_H:  return (unsigned short)va_arg(*args, unsigned int);
        case LEN_L:  return va_arg(*args, unsigned long);
        case LEN_LL: return va_arg(*args, unsigned long long);
        case LEN_Z:  return va_arg(*args, size_t);
        default:     return va_arg(*args, unsigned int);
    }
}

static long long get_signed_arg(va_list *args, length_t len) {
    switch (len) {
        case LEN_HH: return (signed char)va_arg(*args, int);
        case LEN_H:  return (short)va_arg(*args, int);
        case LEN_L:  return va_arg(*args, long);
        case LEN_LL: return va_arg(*args, long long);
        case LEN_Z:  return (long long)va_arg(*args, size_t);
        default:     return va_arg(*args, int);
    }
}

void vstrfmt(void (*outc)(char), const char *fmt, va_list args) {
    if (!outc || !fmt) return;

    while (*fmt) {
        if (*fmt != '%') {
            outc(*fmt++);
            continue;
        }
        fmt++; // skip '%'

        // ---- flags ----
        int left = 0;
        int zero = 0;
        int plus = 0;
        int space = 0;
        int alt = 0;

        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; continue; }
            if (*fmt == '0') { zero = 1; fmt++; continue; }
            if (*fmt == '+') { plus = 1; fmt++; continue; }
            if (*fmt == ' ') { space = 1; fmt++; continue; }
            if (*fmt == '#') { alt = 1; fmt++; continue; }
            break;
        }

        // ---- width ----
        int width = 0;
        if (*fmt == '*') {
            fmt++;
            width = va_arg(args, int);
            if (width < 0) { left = 1; width = -width; }
        } else if (is_digit(*fmt)) {
            width = parse_int(&fmt);
        }

        // ---- precision ----
        int prec = -1; // -1 means "not specified"
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                fmt++;
                prec = va_arg(args, int);
                if (prec < 0) prec = -1; // like printf
            } else {
                prec = is_digit(*fmt) ? parse_int(&fmt) : 0;
            }
        }

        // ---- length ----
        length_t len = LEN_NONE;
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { fmt++; len = LEN_HH; }
            else len = LEN_H;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { fmt++; len = LEN_LL; }
            else len = LEN_L;
        } else if (*fmt == 'z') {
            fmt++;
            len = LEN_Z;
        }

        // spec
        char spec = *fmt ? *fmt++ : '\0';
        if (!spec) break;

        // When precision is specified for integers, '0' flag is ignored unless left aligned.
        if (prec >= 0) zero = 0;

        switch (spec) {
            case '%':
                outc('%');
                break;

            case 'c': {
                char ch = (char)va_arg(args, int);
                int pad = (width > 1) ? (width - 1) : 0;
                if (!left) emit_repeat(outc, ' ', pad);
                outc(ch);
                if (left) emit_repeat(outc, ' ', pad);
                break;
            }

            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";

                int slen = (int)strlen(s);
                if (prec >= 0 && prec < slen) slen = prec;

                int pad = (width > slen) ? (width - slen) : 0;
                if (!left) emit_repeat(outc, ' ', pad);
                emit_strn(outc, s, slen);
                if (left) emit_repeat(outc, ' ', pad);
                break;
            }

            case 'd':
            case 'i': {
                // FIXED: Pass &args (pointer)
                long long v = get_signed_arg(&args, len);
                unsigned long long uv;
                char signch = 0;

                if (v < 0) {
                    signch = '-';
                    // avoid overflow on LLONG_MIN: use two's complement trick
                    uv = (unsigned long long)(~(unsigned long long)v) + 1ULL;
                } else {
                    if (plus) signch = '+';
                    else if (space) signch = ' ';
                    uv = (unsigned long long)v;
                }

                char num[65];
                int nlen = utoa_ull(num, uv, 10, 0);

                // precision: minimum digits
                int zpad = 0;
                if (prec > nlen) zpad = prec - nlen;

                int total = nlen + zpad + (signch ? 1 : 0);

                char padch = (zero && !left) ? '0' : ' ';
                int wpad = (width > total) ? (width - total) : 0;

                if (!left && padch == ' ') emit_repeat(outc, ' ', wpad);
                if (signch) outc(signch);
                if (!left && padch == '0') emit_repeat(outc, '0', wpad);

                emit_repeat(outc, '0', zpad);
                emit_strn(outc, num, nlen);

                if (left) emit_repeat(outc, ' ', wpad);
                break;
            }

            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'b': {
                unsigned base = 10;
                int uppercase = 0;
                const char *prefix = "";
                int prefix_len = 0;

                if (spec == 'x' || spec == 'X') { base = 16; uppercase = (spec == 'X'); }
                else if (spec == 'o') { base = 8; }
                else if (spec == 'b') { base = 2; }

                // FIXED: Pass &args (pointer)
                unsigned long long v = get_unsigned_arg(&args, len);

                // conversion (v==0 with precision==0 -> empty per printf)
                char num[65];
                int nlen = 0;
                if (!(prec == 0 && v == 0)) {
                    nlen = utoa_ull(num, v, base, uppercase);
                }

                // alternate form
                if (alt) {
                    if ((spec == 'x' || spec == 'X') && v != 0) {
                        prefix = "0x";
                        prefix_len = 2;
                    } else if (spec == 'b' && v != 0) {
                        prefix = "0b";
                        prefix_len = 2;
                    } else if (spec == 'o') {
                        // '#' for octal => ensure a leading zero when it would not otherwise exist
                        if (v != 0 && (prec <= nlen)) {
                            prefix = "0";
                            prefix_len = 1;
                        }
                        if (v == 0 && prec == 0) {
                            num[0] = '0'; num[1] = '\0';
                            nlen = 1;
                        }
                    }
                }

                // precision: minimum digits
                int zpad = 0;
                if (prec > nlen) zpad = prec - nlen;

                int total = prefix_len + zpad + nlen;

                char padch = (zero && !left) ? '0' : ' ';
                int wpad = (width > total) ? (width - total) : 0;

                if (!left && padch == ' ') emit_repeat(outc, ' ', wpad);

                if (prefix_len) emit_strn(outc, prefix, prefix_len);

                // width zero-padding goes after prefix
                if (!left && padch == '0') emit_repeat(outc, '0', wpad);

                emit_repeat(outc, '0', zpad);
                if (nlen) emit_strn(outc, num, nlen);

                if (left) emit_repeat(outc, ' ', wpad);
                break;
            }

            case 'p':
            case 'P': {
                // Pointer: always prints 0x/0X and defaults precision to pointer width.
                uintptr_t pv = (uintptr_t)va_arg(args, void *);
                int uppercase = (spec == 'P');

                const char *prefix = "0x";
                int prefix_len = 2;

                int ptr_digits = (int)(sizeof(void*) * 2); // hex digits

                int eff_prec = prec;
                if (eff_prec < 0) eff_prec = ptr_digits;

                char num[65];
                int nlen = 0;
                if (!(eff_prec == 0 && pv == 0)) {
                    nlen = utoa_ull(num, (unsigned long long)pv, 16, uppercase);
                }

                int zpad = 0;
                if (eff_prec > nlen) zpad = eff_prec - nlen;

                int total = prefix_len + zpad + nlen;

                char padch = (zero && !left) ? '0' : ' ';
                int wpad = (width > total) ? (width - total) : 0;

                if (!left && padch == ' ') emit_repeat(outc, ' ', wpad);

                emit_strn(outc, prefix, prefix_len);

                // width zero-padding goes after 0x/0X
                if (!left && padch == '0') emit_repeat(outc, '0', wpad);

                emit_repeat(outc, '0', zpad);
                if (nlen) emit_strn(outc, num, nlen);

                if (left) emit_repeat(outc, ' ', wpad);
                break;
            }

            default:
                // Unknown spec: print it literally
                outc(spec);
                break;
        }
    }
}