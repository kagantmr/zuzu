#ifndef MATH_H
#define MATH_H

#include <float.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Common constants */
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())

#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

/* Classification and comparison helpers */
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf_sign(x)
#define isfinite(x) __builtin_isfinite(x)

#define signbit(x) __builtin_signbit(x)

    /* Declarations (implementations may be provided by your runtime/libm). */
    double acos(double x);
    double asin(double x);
    double atan(double x);
    double atan2(double y, double x);
    double ceil(double x);
    double cos(double x);
    double cosh(double x);
    double exp(double x);
    double floor(double x);
    double fmod(double x, double y);
    double frexp(double x, int *exponent);
    double ldexp(double x, int exponent);
    double log(double x);
    double log10(double x);
    double modf(double x, double *ipart);
    double pow(double x, double y);
    double sin(double x);
    double sinh(double x);
    double sqrt(double x);
    double tan(double x);
    double tanh(double x);

    long double acosl(long double x);
    long double asinl(long double x);
    long double atanl(long double x);
    long double atan2l(long double y, long double x);
    long double ceill(long double x);
    long double cosl(long double x);
    long double coshl(long double x);
    long double expl(long double x);
    long double floorl(long double x);
    long double fmodl(long double x, long double y);
    long double frexpl(long double x, int *exponent);
    long double ldexpl(long double x, int exponent);
    long double logl(long double x);
    long double log10l(long double x);
    long double modfl(long double x, long double *ipart);
    long double powl(long double x, long double y);
    long double sinl(long double x);
    long double sinhl(long double x);
    long double sqrtl(long double x);
    long double tanl(long double x);
    long double tanhl(long double x);

    /* Minimal function set often expected by freestanding code */
    static inline double fabs(double x) { return __builtin_fabs(x); }
    static inline double __fabs__(double x) { return __builtin_fabs(x); }
    static inline float fabsf(float x) { return __builtin_fabsf(x); }
    static inline long double fabsl(long double x) { return __builtin_fabsl(x); }

#ifdef __cplusplus
}
#endif

#endif