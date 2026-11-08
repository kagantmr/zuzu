/**
 * @file strfmt_float.c
 * @brief Kernel stand-in for strfmt_float()
 */

#include <string.h>

int strfmt_float(va_list *args, char spec, int prec, int alt, int plus, int space, char *numbuf,
                 char *signch_out)
{
    (void)args;
    (void)prec;
    (void)alt;
    (void)plus;
    (void)space;
    numbuf[0] = spec;
    *signch_out = 0;
    return 1;
}
