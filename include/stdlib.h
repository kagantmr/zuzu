#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <malloc.h>

#ifdef __cplusplus
extern "C" {
#endif

int atoi(const char *str);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

int abs(int j);

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

int rand(void);
void srand(unsigned int seed);

#ifdef __cplusplus
}
#endif

#endif /* STDLIB_H */

