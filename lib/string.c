#include "lib/string.h"
#include <stdint.h>
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

static int parse_width(const char **pfmt) {
    const char *p = *pfmt;
    int w = 0;
    while (*p >= '0' && *p <= '9') {
        w = (w * 10) + (*p - '0');
        p++;
    }
    *pfmt = p;
    return w;
}

void vstrfmt(void (*outc)(char), const char *fstring, va_list args) {
    if (!outc || !fstring) {
        return;
    }
    while (*fstring) {
        if (*fstring == '%') {
            fstring++; // skip '%'

            int left = 0;
            int zero = 0;

            // flags
            for (;;) {
                if (*fstring == '-') { left = 1; fstring++; continue; }
                if (*fstring == '0') { zero = 1; fstring++; continue; }
                break;
            }

            // width
            int width = 0;
            if (*fstring >= '0' && *fstring <= '9') {
                const char *p = fstring;
                width = parse_width(&p);
                fstring = p;
            }

            switch (*fstring) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    int pad = (width > 1) ? (width - 1) : 0;
                    if (!left) emit_repeat(outc, ' ', pad);
                    outc(c);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case 's': {
                    char *s = va_arg(args, char *);
                    kassert(s != NULL);
                    int len = (int)strlen(s);
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = ' '; // zero flag ignored for strings
                    if (!left) emit_repeat(outc, padch, pad);
                    while (*s) outc(*s++);
                    if (left) emit_repeat(outc, padch, pad);
                    break;
                }
                case 'd': {
                    int d = va_arg(args, int);
                    char bfr[32];

                    // handle sign separately for correct zero-padding
                    if (d < 0) {
                        unsigned int ud = (unsigned int)(-(d + 1)) + 1u; // avoid INT_MIN overflow
                        char nbfr[32];
                        utoa(ud, nbfr, 10);
                        int nlen = (int)strlen(nbfr);
                        int total = nlen + 1; // include '-'
                        int pad = (width > total) ? (width - total) : 0;
                        char padch = (zero && !left) ? '0' : ' ';

                        if (!left && padch == ' ') emit_repeat(outc, ' ', pad);
                        outc('-');
                        if (!left && padch == '0') emit_repeat(outc, '0', pad);
                        for (int i = 0; nbfr[i]; i++) outc(nbfr[i]);
                        if (left) emit_repeat(outc, ' ', pad);
                    } else {
                        utoa((unsigned int)d, bfr, 10);
                        int len = (int)strlen(bfr);
                        int pad = (width > len) ? (width - len) : 0;
                        char padch = (zero && !left) ? '0' : ' ';
                        if (!left) emit_repeat(outc, padch, pad);
                        for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                        if (left) emit_repeat(outc, ' ', pad);
                    }
                    break;
                }
                case 'u': {
                    unsigned int d = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(d, bfr, 10);
                    int len = (int)strlen(bfr);
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = (zero && !left) ? '0' : ' ';
                    if (!left) emit_repeat(outc, padch, pad);
                    for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case 'p': {
                    void* p = va_arg(args, void*);
                    char bfr[32];
                    utoa((uintptr_t)p, bfr, 16);
                    int len = (int)strlen(bfr) + 2; // include 0x
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = (zero && !left) ? '0' : ' ';
                    if (!left) emit_repeat(outc, padch, pad);
                    outc('0'); outc('x');
                    for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case 'x': {
                    unsigned int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 16);
                    int len = (int)strlen(bfr);
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = (zero && !left) ? '0' : ' ';
                    if (!left) emit_repeat(outc, padch, pad);
                    for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case 'o': {
                    unsigned int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 8);
                    int len = (int)strlen(bfr);
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = (zero && !left) ? '0' : ' ';
                    if (!left) emit_repeat(outc, padch, pad);
                    for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case 'b': {
                    unsigned int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 2);
                    int len = (int)strlen(bfr);
                    int pad = (width > len) ? (width - len) : 0;
                    char padch = (zero && !left) ? '0' : ' ';
                    if (!left) emit_repeat(outc, padch, pad);
                    for (int i = 0; bfr[i]; i++) outc(bfr[i]);
                    if (left) emit_repeat(outc, ' ', pad);
                    break;
                }
                case '%':
                    outc('%');
                    break;
                default:
                    outc(*fstring);
                    break;
            }

            if (*fstring) fstring++;
        } else {
            outc(*fstring++);
        }
    }
}
