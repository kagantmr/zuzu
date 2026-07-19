#include "stdlib.h"
#include <convert.h>
#include <stddef.h>
#include <stdint.h>

/* Simple abs */
int abs(int j) { return j < 0 ? -j : j; }

/* strtol/strtoul: basic implementation, supports base 0 and 2..36, 0x prefix */
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    return s;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = skip_ws(nptr);
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }

    if (base == 0) {
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') base = 16;
            else base = 8;
        } else base = 10;
    }

    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    unsigned long acc = 0;
    const char *start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * base + digit;
        s++;
    }

    if (endptr) *endptr = (char *)(s == start ? nptr : s);
    long res = (long)acc;
    return neg ? -res : res;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = skip_ws(nptr);
    if (*s == '+' || *s == '-') { if (*s == '-') { /* treat negative as unsigned wrap */ } s++; }

    if (base == 0) {
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') base = 16;
            else base = 8;
        } else base = 10;
    }

    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    unsigned long acc = 0;
    const char *start = s;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * base + digit;
        s++;
    }

    if (endptr) *endptr = (char *)(s == start ? nptr : s);
    return acc;
}

/* Simple strtod: limited but practical implementation */
double strtod(const char *nptr, char **endptr) {
    const char *s = skip_ws(nptr);
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }

    double val = 0.0;
    while (*s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); s++; }

    if (*s == '.') {
        s++;
        double frac = 0.0;
        double base = 1.0;
        while (*s >= '0' && *s <= '9') { frac = frac * 10.0 + (*s - '0'); base *= 10.0; s++; }
        val += frac / base;
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int expneg = 0;
        if (*s == '+' || *s == '-') { expneg = (*s == '-'); s++; }
        int exp = 0;
        while (*s >= '0' && *s <= '9') { exp = exp * 10 + (*s - '0'); s++; }
        double pow10 = 1.0;
        while (exp--) pow10 *= 10.0;
        if (expneg) val /= pow10; else val *= pow10;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

/* qsort: simple quicksort with recursion */
static void swap_bytes(char *a, char *b, size_t size) {
    while (size--) {
        char t = *a; *a++ = *b; *b++ = t;
    }
}

static void qsort_inner(char *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb < 2) return;
    char *lo = base;
    char *hi = base + (nmemb - 1) * size;
    char *pivot = base + (nmemb/2) * size;
    /* move pivot to end */
    swap_bytes(pivot, hi, size);
    char *store = lo;
    for (char *p = lo; p < hi; p += size) {
        if (compar(p, hi) < 0) {
            swap_bytes(p, store, size);
            store += size;
        }
    }
    swap_bytes(store, hi, size);
    size_t left = (store - base) / size;
    qsort_inner(base, left, size, compar);
    qsort_inner(store + size, nmemb - left - 1, size, compar);
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb == 0 || size == 0) return;
    qsort_inner((char *)base, nmemb, size, compar);
}

/* bsearch lives in klib/bsearch.c: the kernel's region lookup
 * (kernel/mm/vmm.c) shares it. */

/* rand/srand: minimal LCG */
static unsigned long rand_state = 1;

void srand(unsigned int seed) { rand_state = seed ? seed : 1; }

int rand(void) {
    rand_state = rand_state * 1103515245UL + 12345UL;
    return (int)((rand_state >> 1) & 0x7FFFFFFF);
}
