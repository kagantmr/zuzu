#include "string.h"
#include <stdint.h>
#include "convert.h"

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

size_t strnlen(const char *s, size_t maxlen)
{
  size_t len;
  for (len = 0; len < maxlen; ++len)
    if (s[len] == '\0')
      break;
  return len;
}

char *strcat(char *dest, const char *src) {
    char *end = dest;

    while (*end) end++;

    strcpy(end, src);

    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *end = dest;

    while (*end) end++;  // find end of dest

    strncpy(end, src, n);     // now copy src starting at the end

    return dest;
}


int strcmp(const char *s1, const char *s2) {
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
    int diff;
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
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;

    while (n > 0 && *src) {
        *dest++ = *src++;
        n--;
    }

    // Pad remaining bytes with NUL to match strncpy semantics.
    while (n > 0) {
        *dest++ = '\0';
        n--;
    }

    return ret;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)(uintptr_t)s;
        }
        s++;
    }
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;

    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);

    return (char *)(uintptr_t)last;
}

void strfmt(void (*outc)(void *ctx, char), void *ctx, const char *fstring, ...) {
    va_list args;
    va_start(args, fstring);
    vstrfmt(outc, ctx, fstring, &args);
    va_end(args);
}

// --- helpers for vstrfmt field width and padding ---
static void emit_repeat(void (*outc)(void *ctx, char), void *ctx, char ch, int count) {
    while (count-- > 0) outc(ctx, ch);
}

static void emit_strn(void (*outc)(void *ctx, char), void *ctx, const char *s, int n) {
    for (int i = 0; i < n; i++) outc(ctx, s[i]);
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
        case LEN_Z:  return va_arg(*args, ptrdiff_t);
        default:     return va_arg(*args, int);
    }
}

// ---- helpers for %f/%e/%g ----
// All double math below stays in floating point (no 64-bit integer divide,
// which utoa_ull's own comment avoids relying on) -- only the final
// truncating cast to unsigned long long is used to pull out decimal digits.

// Fills `digits[0..prec-1]` with frac's decimal expansion (frac in [0,1)),
// rounding the last digit half-up. *carry_out is 1 if the rounding rippled
// past the first digit (caller must then bump the integer part by one).
static void format_fixed_digits(double frac, int prec, char *digits, int *carry_out) {
    for (int i = 0; i < prec; i++) {
        frac *= 10.0;
        int d = (int)frac;
        if (d > 9) d = 9;
        if (d < 0) d = 0;
        digits[i] = (char)('0' + d);
        frac -= d;
    }
    *carry_out = 0;
    if (frac >= 0.5) {
        int i = prec - 1;
        while (i >= 0) {
            if (digits[i] == '9') { digits[i] = '0'; i--; }
            else { digits[i] = (char)(digits[i] + 1); break; }
        }
        if (i < 0) *carry_out = 1;
    }
}

// Normalizes av (> 0) to `sigdigits` significant decimal digits (rounded)
// plus a base-10 exponent, i.e. av == 0.d0d1...d(sigdigits-1) * 10^(exp+1).
static void format_efmt_digits(double av, int sigdigits, char *digits, int *exp_out) {
    int exp = 0;
    while (av >= 10.0) { av /= 10.0; exp++; }
    while (av < 1.0)   { av *= 10.0; exp--; }

    double scale = 1.0;
    for (int i = 0; i < sigdigits - 1; i++) scale *= 10.0;

    double rounded = av + 0.5 / scale;
    if (rounded >= 10.0) { rounded /= 10.0; exp++; }

    unsigned long long scaled = (unsigned long long)(rounded * scale);

    char tmp[24];
    int n = utoa_ull(tmp, scaled, 10, 0);
    int pad = sigdigits - n;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) digits[i] = '0';
    for (int i = 0; i < n && (pad + i) < sigdigits; i++) digits[pad + i] = tmp[i];

    *exp_out = exp;
}

void vstrfmt(void (*outc)(void *ctx, char), void *ctx, const char *fmt, va_list *args) {
    if (!outc || !fmt) return;

    while (*fmt) {
        if (*fmt != '%') {
            outc(ctx, *fmt++);
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
            width = va_arg(*args, int);
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
                prec = va_arg(*args, int);
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
                outc(ctx, '%');
                break;

            case 'c': {
                char ch = (char)va_arg(*args, int);
                int pad = (width > 1) ? (width - 1) : 0;
                if (!left) emit_repeat(outc, ctx, ' ', pad);
                outc(ctx, ch);
                if (left) emit_repeat(outc,ctx, ' ', pad);
                break;
            }

            case 's': {
                const char *s = va_arg(*args, const char *);
                if (!s) s = "(null)";

                int slen = (int)strlen(s);
                if (prec >= 0 && prec < slen) slen = prec;

                int pad = (width > slen) ? (width - slen) : 0;
                if (!left) emit_repeat(outc, ctx, ' ', pad);
                emit_strn(outc, ctx, s, slen);
                if (left) emit_repeat(outc, ctx, ' ', pad);
                break;
            }

            case 'd':
            case 'i': {
                // FIXED: Pass &args (pointer)
                long long v = get_signed_arg(args, len);
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

                if (!left && padch == ' ') emit_repeat(outc, ctx, ' ', wpad);
                if (signch) outc(ctx, signch);
                if (!left && padch == '0') emit_repeat(outc, ctx, '0', wpad);

                emit_repeat(outc, ctx, '0', zpad);
                emit_strn(outc, ctx, num, nlen);

                if (left) emit_repeat(outc, ctx, ' ', wpad);
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
                unsigned long long v = get_unsigned_arg(args, len);

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

                if (!left && padch == ' ') emit_repeat(outc, ctx, ' ', wpad);

                if (prefix_len) emit_strn(outc, ctx, prefix, prefix_len);

                // width zero-padding goes after prefix
                if (!left && padch == '0') emit_repeat(outc, ctx, '0', wpad);

                emit_repeat(outc, ctx, '0', zpad);
                if (nlen) emit_strn(outc, ctx, num, nlen);

                if (left) emit_repeat(outc, ctx, ' ', wpad);
                break;
            }

            case 'p':
            case 'P': {
                // Pointer: always prints 0x/0X and defaults precision to pointer width.
                uintptr_t pv = (uintptr_t)va_arg(*args, void *);
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

                if (!left && padch == ' ') emit_repeat(outc, ctx, ' ', wpad);

                emit_strn(outc, ctx, prefix, prefix_len);

                // width zero-padding goes after 0x/0X
                if (!left && padch == '0') emit_repeat(outc, ctx, '0', wpad);

                emit_repeat(outc, ctx, '0', zpad);
                if (nlen) emit_strn(outc, ctx, num, nlen);

                if (left) emit_repeat(outc, ctx, ' ', wpad);
                break;
            }

            case 'f': case 'F':
            case 'e': case 'E':
            case 'g': case 'G': {
                double val = va_arg(*args, double);
                int uppercase = (spec == 'F' || spec == 'E' || spec == 'G');
                char base_spec = (char)(spec | 0x20); // 'f', 'e' or 'g'

                char signch = 0;
                if (__builtin_signbit(val)) signch = '-';
                else if (plus) signch = '+';
                else if (space) signch = ' ';

                double av = __builtin_fabs(val);
                char numbuf[64];
                int nlen = 0;

                if (__builtin_isnan(val)) {
                    const char *s = uppercase ? "NAN" : "nan";
                    numbuf[0] = s[0]; numbuf[1] = s[1]; numbuf[2] = s[2];
                    nlen = 3;
                    signch = 0; // NaN never carries a sign
                } else if (__builtin_isinf(val)) {
                    const char *s = uppercase ? "INF" : "inf";
                    numbuf[0] = s[0]; numbuf[1] = s[1]; numbuf[2] = s[2];
                    nlen = 3;
                } else if (base_spec == 'f') {
                    int p = (prec < 0) ? 6 : prec;
                    if (p > 40) p = 40;

                    unsigned long long ip = (unsigned long long)av;
                    double frac = av - (double)ip;

                    char fdigits[41];
                    int carry = 0;
                    format_fixed_digits(frac, p, fdigits, &carry);
                    if (carry) ip += 1;

                    char ibuf[24];
                    int ilen = utoa_ull(ibuf, ip, 10, 0);
                    for (int i = 0; i < ilen; i++) numbuf[nlen++] = ibuf[i];
                    if (p > 0 || alt) numbuf[nlen++] = '.';
                    for (int i = 0; i < p; i++) numbuf[nlen++] = fdigits[i];
                } else if (base_spec == 'e') {
                    int p = (prec < 0) ? 6 : prec;
                    int sig = p + 1;
                    if (sig > 17) sig = 17;

                    int exp10 = 0;
                    char mdigits[17];
                    if (av == 0.0) {
                        for (int i = 0; i < sig; i++) mdigits[i] = '0';
                    } else {
                        format_efmt_digits(av, sig, mdigits, &exp10);
                    }

                    numbuf[nlen++] = mdigits[0];
                    if (p > 0 || alt) {
                        numbuf[nlen++] = '.';
                        for (int i = 1; i < sig; i++) numbuf[nlen++] = mdigits[i];
                    }
                    numbuf[nlen++] = uppercase ? 'E' : 'e';
                    numbuf[nlen++] = (exp10 < 0) ? '-' : '+';
                    int aexp = (exp10 < 0) ? -exp10 : exp10;
                    char ebuf[8];
                    int elen = utoa_ull(ebuf, (unsigned long long)aexp, 10, 0);
                    if (elen < 2) numbuf[nlen++] = '0';
                    for (int i = 0; i < elen; i++) numbuf[nlen++] = ebuf[i];
                } else { // 'g'/'G'
                    int p = (prec < 0) ? 6 : (prec == 0 ? 1 : prec);
                    if (p > 17) p = 17;

                    int exp10 = 0;
                    char mdigits[17];
                    if (av == 0.0) {
                        for (int i = 0; i < p; i++) mdigits[i] = '0';
                    } else {
                        format_efmt_digits(av, p, mdigits, &exp10);
                    }

                    if (exp10 < -4 || exp10 >= p) {
                        // %e-style, precision p-1, trailing mantissa zeros trimmed
                        int fdig = p - 1;
                        if (!alt) {
                            while (fdig > 0 && mdigits[fdig] == '0') fdig--;
                        }
                        numbuf[nlen++] = mdigits[0];
                        if (fdig > 0 || alt) {
                            numbuf[nlen++] = '.';
                            for (int i = 1; i <= fdig; i++) numbuf[nlen++] = mdigits[i];
                        }
                        numbuf[nlen++] = uppercase ? 'E' : 'e';
                        numbuf[nlen++] = (exp10 < 0) ? '-' : '+';
                        int aexp = (exp10 < 0) ? -exp10 : exp10;
                        char ebuf[8];
                        int elen = utoa_ull(ebuf, (unsigned long long)aexp, 10, 0);
                        if (elen < 2) numbuf[nlen++] = '0';
                        for (int i = 0; i < elen; i++) numbuf[nlen++] = ebuf[i];
                    } else {
                        // %f-style, p - 1 - exp10 digits after the point
                        int fracdigits = p - 1 - exp10;
                        if (fracdigits < 0) fracdigits = 0;

                        if (exp10 >= 0) {
                            for (int i = 0; i <= exp10; i++) numbuf[nlen++] = mdigits[i];
                        } else {
                            numbuf[nlen++] = '0';
                        }
                        if (fracdigits > 0 || alt) {
                            numbuf[nlen++] = '.';
                            if (exp10 < 0) {
                                for (int i = 0; i < (-exp10 - 1); i++) numbuf[nlen++] = '0';
                                for (int i = 0; i < p; i++) numbuf[nlen++] = mdigits[i];
                            } else {
                                for (int i = exp10 + 1; i < p; i++) numbuf[nlen++] = mdigits[i];
                            }
                            if (!alt) {
                                while (nlen > 0 && numbuf[nlen - 1] == '0') nlen--;
                                if (nlen > 0 && numbuf[nlen - 1] == '.') nlen--;
                            }
                        }
                    }
                }

                int total = nlen + (signch ? 1 : 0);
                char padch = (zero && !left) ? '0' : ' ';
                int wpad = (width > total) ? (width - total) : 0;

                if (!left && padch == ' ') emit_repeat(outc, ctx, ' ', wpad);
                if (signch) outc(ctx, signch);
                if (!left && padch == '0') emit_repeat(outc, ctx, '0', wpad);
                emit_strn(outc, ctx, numbuf, nlen);
                if (left) emit_repeat(outc, ctx, ' ', wpad);
                break;
            }

            default:
                // Unknown spec: print it literally
                outc(ctx, spec);
                break;
        }
    }
}

int visible_len(const char *s)
{
    int len = 0;
    while (*s) {
        if (*s == '\033') {
            while (*s && *s != 'm') s++;
            if (*s) s++;
        } else {
            len++;
            s++;
        }
    }
    return len;
}
