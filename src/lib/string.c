#include "lib/string.h"

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}


char *strcat(char *dest, const char *src);

char *strncat(char *dest, const char *src, size_t n);


char *strcpy(char *dest, const char *src);


int strcmp(const char *s1, const char *s2);


int strncmp(const char *s1, const char *s2, size_t n);


char *strncpy(char *dest, const char *src, size_t n);


char *strfmt(const char *fstring, ...);