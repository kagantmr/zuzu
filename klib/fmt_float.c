/**
 * @file fmt_float.c
 * @brief Floating-point conversions for vstrfmt(). linked into zcrt only.
 */

#include "string.h"

/**
 * @brief Writes an unsigned integer in decimal.
 *
 * @param[out] buf Receives the digits and a terminating NUL; needs room for 21 characters.
 * @param[in]  v   Value to write.
 *
 * @return Number of digits written, not counting the NUL.
 */
static int utoa_dec(char *buf, unsigned long long v)
{
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
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

    double rounded = av + (0.5 / scale);
    if (rounded >= 10.0) {
        rounded /= 10.0;
        exp++;
    }

    unsigned long long scaled = (unsigned long long)(rounded * scale);

    char tmp[24];
    int n = utoa_dec(tmp, scaled);
    int pad = sigdigits - n;
    if (pad < 0)
        pad = 0;
    for (int i = 0; i < pad; i++)
        digits[i] = '0';
    for (int i = 0; i < n && (pad + i) < sigdigits; i++)
        digits[pad + i] = tmp[i];

    *exp_out = exp;
}

int strfmt_float(va_list *args, char spec, int prec, int alt, int plus, int space, char *numbuf,
                 char *signch_out)
{
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
        int ilen = utoa_dec(ibuf, ip);
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
        int elen = utoa_dec(ebuf, (unsigned long long)aexp);
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
            int elen = utoa_dec(ebuf, (unsigned long long)aexp);
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

    *signch_out = signch;
    return nlen;
}
