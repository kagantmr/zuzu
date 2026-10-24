/**
 * @file string.c
 * @brief String and memory-block functions and the printf-style formatting core.
 */

#include "string.h"
#include <stdint.h>

size_t strlen(const char *s)
{
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

int strcmp(const char *s1, const char *s2)
{
    int diff;
    while (*s1 && *s2) {
        diff = *s1++ - *s2++;
        if (diff != 0) {
            return diff;
        };
    }

    return *s1 - *s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    int diff;
    size_t compared = 0;
    while (*s1 && *s2 && compared < n) {
        diff = *s1++ - *s2++;
        if (diff != 0) {
            return diff;
        };
        compared++;
    }
    if (compared == n)
        return 0;
    return *s1 - *s2;
}

char *strcpy(char *dest, const char *src)
{
    char *ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *ret = dest;

    while (n > 0 && *src) {
        *dest++ = *src++;
        n--;
    }

    while (n > 0) {
        *dest++ = '\0';
        n--;
    }

    return ret;
}

char *strcat(char *dest, const char *src)
{
    char *end = dest;
    while (*end)
        end++;
    while ((*end++ = *src++))
        ;
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *end = dest;
    while (*end)
        end++;
    while (n > 0 && *src) {
        *end++ = *src++;
        n--;
    }
    *end = '\0';
    return dest;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) {
            return (char *)(uintptr_t)s;
        }
        s++;
    }
    return NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);

    return (char *)(uintptr_t)last;
}

/**
 * @brief Emits one character several times.
 *
 * @param[in] outc Character sink.
 * @param[in,out] ctx Passed to @p outc.
 * @param[in] ch Character to emit.
 * @param[in] count Number of repetitions; nothing is emitted if not positive.
 */
static void emit_repeat(void (*outc)(void *ctx, char), void *ctx, char ch, int count)
{
    while (count-- > 0)
        outc(ctx, ch);
}

/**
 * @brief Emits the first @p n characters of a string.
 *
 * @param[in] outc Character sink.
 * @param[in,out] ctx Passed to @p outc.
 * @param[in] s Characters to emit.
 * @param[in] n Number of characters.
 */
static void emit_strn(void (*outc)(void *ctx, char), void *ctx, const char *s, int n)
{
    for (int i = 0; i < n; i++)
        outc(ctx, s[i]);
}

/**
 * @brief Tells whether a character is a decimal digit.
 *
 * @param[in] c Character to test.
 * @return Nonzero for '0' to '9', otherwise 0.
 */
static int is_digit(char c) { return (c >= '0' && c <= '9'); }

/**
 * @brief Parses a run of decimal digits and advances past it.
 *
 * @param[in,out] pp Points at the first digit; updated to the first non-digit.
 * @return The parsed value.
 */
static int parse_int(const char **pp)
{
    const char *p = *pp;
    int v = 0;
    while (is_digit(*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

/**
 * @brief Writes an unsigned number as text, NUL-terminated.
 *
 * Supports bases 2, 8, 10 and 16; a base below 2 is treated as 10, and any other
 * base yields "?". @p buf must hold at least 66 bytes.
 *
 * @param[out] buf Destination.
 * @param[in] v Value to convert.
 * @param[in] base Numeric base.
 * @param[in] uppercase Nonzero to use A-F instead of a-f for base 16.
 * @return The number of characters written, excluding the terminator.
 */
static int utoa_ull(char *buf, unsigned long long v, unsigned base, int uppercase)
{
    static const char digs_lo[] = "0123456789abcdef";
    static const char digs_hi[] = "0123456789ABCDEF";
    const char *digs = uppercase ? digs_hi : digs_lo;

    char tmp[65];
    int n = 0;

    if (base < 2)
        base = 10;

    if (base == 16) {
        if (v == 0)
            tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0xFULL)];
            v >>= 4;
        }
    } else if (base == 8) {
        if (v == 0)
            tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0x7ULL)];
            v >>= 3;
        }
    } else if (base == 2) {
        if (v == 0)
            tmp[n++] = '0';
        while (v != 0) {
            tmp[n++] = digs[(unsigned)(v & 0x1ULL)];
            v >>= 1;
        }
    } else if (base == 10) {
        if (v == 0) {
            tmp[n++] = '0';
        }
        while (v != 0) {
            tmp[n++] = (char)('0' + (v % 10ULL));
            v /= 10ULL;
        }
    } else {
        tmp[n++] = '?';
    }

    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

/** @brief Length modifier of a conversion specification. */
typedef enum {
    LEN_NONE, /**< No modifier. */
    LEN_HH, /**< hh: char. */
    LEN_H, /**< h: short. */
    LEN_L, /**< l: long. */
    LEN_LL, /**< ll: long long. */
    LEN_Z /**< z: size_t. */
} length_t;

/**
 * @brief Fetches the next unsigned integer argument, sized by the length modifier.
 *
 * @param[in,out] args Argument list, advanced past the value.
 * @param[in] len Length modifier.
 * @return The argument, narrowed for hh and h and widened to 64 bits.
 */
static unsigned long long get_unsigned_arg(va_list *args, length_t len)
{
    switch (len) {
    case LEN_HH:
        return (unsigned char)va_arg(*args, unsigned int);
    case LEN_H:
        return (unsigned short)va_arg(*args, unsigned int);
    case LEN_L:
        return va_arg(*args, unsigned long);
    case LEN_LL:
        return va_arg(*args, unsigned long long);
    case LEN_Z:
        return va_arg(*args, size_t);
    default:
        return va_arg(*args, unsigned int);
    }
}

/**
 * @brief Fetches the next signed integer argument, sized by the length modifier.
 *
 * @param[in,out] args Argument list, advanced past the value.
 * @param[in] len Length modifier.
 * @return The argument, narrowed for hh and h and widened to 64 bits.
 */
static long long get_signed_arg(va_list *args, length_t len)
{
    switch (len) {
    case LEN_HH:
        return (signed char)va_arg(*args, int);
    case LEN_H:
        return (short)va_arg(*args, int);
    case LEN_L:
        return va_arg(*args, long);
    case LEN_LL:
        return va_arg(*args, long long);
    case LEN_Z:
        return va_arg(*args, ptrdiff_t);
    default:
        return va_arg(*args, int);
    }
}

/**
 * @brief Produces the digits of a fraction for %f, rounding to nearest.
 *
 * @param[in] frac Fractional part, in [0, 1).
 * @param[in] prec Number of digits to produce.
 * @param[out] digits Receives @p prec characters, not terminated.
 * @param[out] carry_out Set to 1 if rounding carried into the integer part, else 0.
 */
static void format_fixed_digits(double frac, int prec, char *digits, int *carry_out)
{
    for (int i = 0; i < prec; i++) {
        frac *= 10.0;
        int d = (int)frac;
        if (d > 9)
            d = 9;
        if (d < 0)
            d = 0;
        digits[i] = (char)('0' + d);
        frac -= d;
    }
    *carry_out = 0;
    if (frac >= 0.5) {
        int i = prec - 1;
        while (i >= 0) {
            if (digits[i] == '9') {
                digits[i] = '0';
                i--;
            } else {
                digits[i] = (char)(digits[i] + 1);
                break;
            }
        }
        if (i < 0)
            *carry_out = 1;
    }
}

/**
 * @brief Produces significant digits and a decimal exponent for %e and %g.
 *
 * @param[in] av Positive value to convert.
 * @param[in] sigdigits Number of significant digits to produce.
 * @param[out] digits Receives @p sigdigits characters, not terminated.
 * @param[out] exp_out Receives the decimal exponent of the first digit.
 */
static void format_efmt_digits(double av, int sigdigits, char *digits, int *exp_out)
{
    int exp = 0;
    while (av >= 10.0) {
        av /= 10.0;
        exp++;
    }
    while (av < 1.0) {
        av *= 10.0;
        exp--;
    }

    double scale = 1.0;
    for (int i = 0; i < sigdigits - 1; i++)
        scale *= 10.0;

    double rounded = av + 0.5 / scale;
    if (rounded >= 10.0) {
        rounded /= 10.0;
        exp++;
    }

    unsigned long long scaled = (unsigned long long)(rounded * scale);

    char tmp[24];
    int n = utoa_ull(tmp, scaled, 10, 0);
    int pad = sigdigits - n;
    if (pad < 0)
        pad = 0;
    for (int i = 0; i < pad; i++)
        digits[i] = '0';
    for (int i = 0; i < n && (pad + i) < sigdigits; i++)
        digits[pad + i] = tmp[i];

    *exp_out = exp;
}

void vstrfmt(strfmt_outc_t outc, void *ctx, const char *fmt, va_list *args)
{
    if (!outc || !fmt)
        return;

    while (*fmt) {
        if (*fmt != '%') {
            outc(ctx, *fmt++);
            continue;
        }
        fmt++;

        int left = 0;
        int zero = 0;
        int plus = 0;
        int space = 0;
        int alt = 0;

        for (;;) {
            if (*fmt == '-') {
                left = 1;
                fmt++;
                continue;
            }
            if (*fmt == '0') {
                zero = 1;
                fmt++;
                continue;
            }
            if (*fmt == '+') {
                plus = 1;
                fmt++;
                continue;
            }
            if (*fmt == ' ') {
                space = 1;
                fmt++;
                continue;
            }
            if (*fmt == '#') {
                alt = 1;
                fmt++;
                continue;
            }
            break;
        }

        int width = 0;
        if (*fmt == '*') {
            fmt++;
            width = va_arg(*args, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
        } else if (is_digit(*fmt)) {
            width = parse_int(&fmt);
        }

        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                fmt++;
                prec = va_arg(*args, int);
                if (prec < 0)
                    prec = -1;
            } else {
                prec = is_digit(*fmt) ? parse_int(&fmt) : 0;
            }
        }

        length_t len = LEN_NONE;
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') {
                fmt++;
                len = LEN_HH;
            } else
                len = LEN_H;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                fmt++;
                len = LEN_LL;
            } else
                len = LEN_L;
        } else if (*fmt == 'z') {
            fmt++;
            len = LEN_Z;
        }

        char spec = *fmt ? *fmt++ : '\0';
        if (!spec)
            break;

        if (prec >= 0)
            zero = 0;

        switch (spec) {
        case '%':
            outc(ctx, '%');
            break;

        case 'c': {
            char ch = (char)va_arg(*args, int);
            int pad = (width > 1) ? (width - 1) : 0;
            if (!left)
                emit_repeat(outc, ctx, ' ', pad);
            outc(ctx, ch);
            if (left)
                emit_repeat(outc, ctx, ' ', pad);
            break;
        }

        case 's': {
            const char *s = va_arg(*args, const char *);
            if (!s)
                s = "(null)";

            int slen = (int)strlen(s);
            if (prec >= 0 && prec < slen)
                slen = prec;

            int pad = (width > slen) ? (width - slen) : 0;
            if (!left)
                emit_repeat(outc, ctx, ' ', pad);
            emit_strn(outc, ctx, s, slen);
            if (left)
                emit_repeat(outc, ctx, ' ', pad);
            break;
        }

        case 'd':
        case 'i': {
            long long v = get_signed_arg(args, len);
            unsigned long long uv;
            char signch = 0;

            if (v < 0) {
                signch = '-';
                uv = (unsigned long long)(~(unsigned long long)v) + 1ULL;
            } else {
                if (plus)
                    signch = '+';
                else if (space)
                    signch = ' ';
                uv = (unsigned long long)v;
            }

            char num[65];
            int nlen = utoa_ull(num, uv, 10, 0);

            int zpad = 0;
            if (prec > nlen)
                zpad = prec - nlen;

            int total = nlen + zpad + (signch ? 1 : 0);

            char padch = (zero && !left) ? '0' : ' ';
            int wpad = (width > total) ? (width - total) : 0;

            if (!left && padch == ' ')
                emit_repeat(outc, ctx, ' ', wpad);
            if (signch)
                outc(ctx, signch);
            if (!left && padch == '0')
                emit_repeat(outc, ctx, '0', wpad);

            emit_repeat(outc, ctx, '0', zpad);
            emit_strn(outc, ctx, num, nlen);

            if (left)
                emit_repeat(outc, ctx, ' ', wpad);
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

            if (spec == 'x' || spec == 'X') {
                base = 16;
                uppercase = (spec == 'X');
            } else if (spec == 'o') {
                base = 8;
            } else if (spec == 'b') {
                base = 2;
            }

            unsigned long long v = get_unsigned_arg(args, len);

            char num[65];
            int nlen = 0;
            if (!(prec == 0 && v == 0)) {
                nlen = utoa_ull(num, v, base, uppercase);
            }

            if (alt) {
                if ((spec == 'x' || spec == 'X') && v != 0) {
                    prefix = "0x";
                    prefix_len = 2;
                } else if (spec == 'b' && v != 0) {
                    prefix = "0b";
                    prefix_len = 2;
                } else if (spec == 'o') {
                    if (v != 0 && (prec <= nlen)) {
                        prefix = "0";
                        prefix_len = 1;
                    }
                    if (v == 0 && prec == 0) {
                        num[0] = '0';
                        num[1] = '\0';
                        nlen = 1;
                    }
                }
            }

            int zpad = 0;
            if (prec > nlen)
                zpad = prec - nlen;

            int total = prefix_len + zpad + nlen;

            char padch = (zero && !left) ? '0' : ' ';
            int wpad = (width > total) ? (width - total) : 0;

            if (!left && padch == ' ')
                emit_repeat(outc, ctx, ' ', wpad);

            if (prefix_len)
                emit_strn(outc, ctx, prefix, prefix_len);

            if (!left && padch == '0')
                emit_repeat(outc, ctx, '0', wpad);

            emit_repeat(outc, ctx, '0', zpad);
            if (nlen)
                emit_strn(outc, ctx, num, nlen);

            if (left)
                emit_repeat(outc, ctx, ' ', wpad);
            break;
        }

        case 'p':
        case 'P': {
            uintptr_t pv = (uintptr_t)va_arg(*args, void *);
            int uppercase = (spec == 'P');

            const char *prefix = "0x";
            int prefix_len = 2;

            int ptr_digits = (int)(sizeof(void *) * 2);

            int eff_prec = prec;
            if (eff_prec < 0)
                eff_prec = ptr_digits;

            char num[65];
            int nlen = 0;
            if (!(eff_prec == 0 && pv == 0)) {
                nlen = utoa_ull(num, (unsigned long long)pv, 16, uppercase);
            }

            int zpad = 0;
            if (eff_prec > nlen)
                zpad = eff_prec - nlen;

            int total = prefix_len + zpad + nlen;

            char padch = (zero && !left) ? '0' : ' ';
            int wpad = (width > total) ? (width - total) : 0;

            if (!left && padch == ' ')
                emit_repeat(outc, ctx, ' ', wpad);

            emit_strn(outc, ctx, prefix, prefix_len);

            if (!left && padch == '0')
                emit_repeat(outc, ctx, '0', wpad);

            emit_repeat(outc, ctx, '0', zpad);
            if (nlen)
                emit_strn(outc, ctx, num, nlen);

            if (left)
                emit_repeat(outc, ctx, ' ', wpad);
            break;
        }

        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            double val = va_arg(*args, double);
            int uppercase = (spec == 'F' || spec == 'E' || spec == 'G');
            char base_spec = (char)(spec | 0x20);

            char signch = 0;
            if (__builtin_signbit(val))
                signch = '-';
            else if (plus)
                signch = '+';
            else if (space)
                signch = ' ';

            double av = __builtin_fabs(val);
            char numbuf[64];
            int nlen = 0;

            if (__builtin_isnan(val)) {
                const char *s = uppercase ? "NAN" : "nan";
                numbuf[0] = s[0];
                numbuf[1] = s[1];
                numbuf[2] = s[2];
                nlen = 3;
                signch = 0;
            } else if (__builtin_isinf(val)) {
                const char *s = uppercase ? "INF" : "inf";
                numbuf[0] = s[0];
                numbuf[1] = s[1];
                numbuf[2] = s[2];
                nlen = 3;
            } else if (base_spec == 'f') {
                int p = (prec < 0) ? 6 : prec;
                if (p > 40)
                    p = 40;

                unsigned long long ip = (unsigned long long)av;
                double frac = av - (double)ip;

                char fdigits[41];
                int carry = 0;
                format_fixed_digits(frac, p, fdigits, &carry);
                if (carry)
                    ip += 1;

                char ibuf[24];
                int ilen = utoa_ull(ibuf, ip, 10, 0);
                for (int i = 0; i < ilen; i++)
                    numbuf[nlen++] = ibuf[i];
                if (p > 0 || alt)
                    numbuf[nlen++] = '.';
                for (int i = 0; i < p; i++)
                    numbuf[nlen++] = fdigits[i];
            } else if (base_spec == 'e') {
                int p = (prec < 0) ? 6 : prec;
                int sig = p + 1;
                if (sig > 17)
                    sig = 17;

                int exp10 = 0;
                char mdigits[17];
                if (av == 0.0) {
                    for (int i = 0; i < sig; i++)
                        mdigits[i] = '0';
                } else {
                    format_efmt_digits(av, sig, mdigits, &exp10);
                }

                numbuf[nlen++] = mdigits[0];
                if (p > 0 || alt) {
                    numbuf[nlen++] = '.';
                    for (int i = 1; i < sig; i++)
                        numbuf[nlen++] = mdigits[i];
                }
                numbuf[nlen++] = uppercase ? 'E' : 'e';
                numbuf[nlen++] = (exp10 < 0) ? '-' : '+';
                int aexp = (exp10 < 0) ? -exp10 : exp10;
                char ebuf[8];
                int elen = utoa_ull(ebuf, (unsigned long long)aexp, 10, 0);
                if (elen < 2)
                    numbuf[nlen++] = '0';
                for (int i = 0; i < elen; i++)
                    numbuf[nlen++] = ebuf[i];
            } else {
                int p = (prec < 0) ? 6 : (prec == 0 ? 1 : prec);
                if (p > 17)
                    p = 17;

                int exp10 = 0;
                char mdigits[17];
                if (av == 0.0) {
                    for (int i = 0; i < p; i++)
                        mdigits[i] = '0';
                } else {
                    format_efmt_digits(av, p, mdigits, &exp10);
                }

                if (exp10 < -4 || exp10 >= p) {
                    int fdig = p - 1;
                    if (!alt) {
                        while (fdig > 0 && mdigits[fdig] == '0')
                            fdig--;
                    }
                    numbuf[nlen++] = mdigits[0];
                    if (fdig > 0 || alt) {
                        numbuf[nlen++] = '.';
                        for (int i = 1; i <= fdig; i++)
                            numbuf[nlen++] = mdigits[i];
                    }
                    numbuf[nlen++] = uppercase ? 'E' : 'e';
                    numbuf[nlen++] = (exp10 < 0) ? '-' : '+';
                    int aexp = (exp10 < 0) ? -exp10 : exp10;
                    char ebuf[8];
                    int elen = utoa_ull(ebuf, (unsigned long long)aexp, 10, 0);
                    if (elen < 2)
                        numbuf[nlen++] = '0';
                    for (int i = 0; i < elen; i++)
                        numbuf[nlen++] = ebuf[i];
                } else {
                    int fracdigits = p - 1 - exp10;
                    if (fracdigits < 0)
                        fracdigits = 0;

                    if (exp10 >= 0) {
                        for (int i = 0; i <= exp10; i++)
                            numbuf[nlen++] = mdigits[i];
                    } else {
                        numbuf[nlen++] = '0';
                    }
                    if (fracdigits > 0 || alt) {
                        numbuf[nlen++] = '.';
                        if (exp10 < 0) {
                            for (int i = 0; i < (-exp10 - 1); i++)
                                numbuf[nlen++] = '0';
                            for (int i = 0; i < p; i++)
                                numbuf[nlen++] = mdigits[i];
                        } else {
                            for (int i = exp10 + 1; i < p; i++)
                                numbuf[nlen++] = mdigits[i];
                        }
                        if (!alt) {
                            while (nlen > 0 && numbuf[nlen - 1] == '0')
                                nlen--;
                            if (nlen > 0 && numbuf[nlen - 1] == '.')
                                nlen--;
                        }
                    }
                }
            }

            int total = nlen + (signch ? 1 : 0);
            char padch = (zero && !left) ? '0' : ' ';
            int wpad = (width > total) ? (width - total) : 0;

            if (!left && padch == ' ')
                emit_repeat(outc, ctx, ' ', wpad);
            if (signch)
                outc(ctx, signch);
            if (!left && padch == '0')
                emit_repeat(outc, ctx, '0', wpad);
            emit_strn(outc, ctx, numbuf, nlen);
            if (left)
                emit_repeat(outc, ctx, ' ', wpad);
            break;
        }

        default:
            outc(ctx, spec);
            break;
        }
    }
}
