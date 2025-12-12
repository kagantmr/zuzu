#include "lib/string.h"
#include <stdint.h>
#include "lib/convert.h"

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}


char *strcat(char *dest, const char *src) {
    char *end = dest;

    while (*end) end++;

    strcpy(end, src);

    *end = '\0';

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
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
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
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return NULL;
}

void strfmt(void (*outc)(char), const char *fstring, ...) {
    va_list args;
    

    va_start(args, fstring);
    vstrfmt(outc, fstring, args);
    va_end(args);
}

void vstrfmt(void (*outc)(char), const char *fstring, va_list args) {
    while (*fstring) {
        if (*fstring == '%') {
            fstring++;
            switch (*fstring) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    outc(c);
                    break;
                }
                case 's': {
                    char *s = va_arg(args, char *);
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'd': {
                    int d = va_arg(args, int);
                    char bfr[32];
                    itoa(d, bfr, 10);
                    char *s = bfr;
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'u': {
                    unsigned int d = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(d, bfr, 10);
                    char *s = bfr;
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'p': {
                    void* p = va_arg(args, void*);
                    char bfr[32];
                    utoa((uintptr_t)p, bfr, 16);
                    char *s = bfr;
                    outc('0'); outc('x');
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'x': {
                    unsigned int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 16);
                    char *s = bfr;
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'o': {
                    unsigned int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 8);
                    char *s = bfr;
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }
                case 'b': {
                    int x = va_arg(args, unsigned int);
                    char bfr[32];
                    utoa(x, bfr, 2);
                    char *s = bfr;
                    while (*s) {
                        outc(*s++);
                    }
                    break;
                }

                case '%':
                    outc('%');
                    break;
                default:
                    outc(*fstring);
            }
            fstring++;
        } else {
            outc(*fstring++);
        }
    }
}
