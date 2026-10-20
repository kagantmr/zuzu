/**
 * @file
 * @brief Floating-point constants, classification macros and libm declarations.
 */

#ifndef MATH_H
#define MATH_H

#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUGE_VAL (__builtin_huge_val()) /**< Overflow result for double. */
#define HUGE_VALF (__builtin_huge_valf()) /**< Overflow result for float. */
#define HUGE_VALL (__builtin_huge_vall()) /**< Overflow result for long double. */

#define INFINITY (__builtin_inff()) /**< Positive infinity, as a float. */
#define NAN (__builtin_nanf("")) /**< A quiet NaN, as a float. */

#define isnan(x) __builtin_isnan(x) /**< Non-zero if @p x is a NaN. */
#define isinf(x) __builtin_isinf_sign(x) /**< 1 for +infinity, -1 for -infinity, 0 otherwise. */
#define isfinite(x) __builtin_isfinite(x) /**< Non-zero if @p x is neither infinite nor a NaN. */

#define signbit(x) __builtin_signbit(x) /**< Non-zero if @p x has its sign bit set. */

/*
 * zuzu's own library does not implement the functions below; they come from
 * newlib's libm in tier-2 programs.
 */
/**
 * @brief Arc cosine of @p x, in radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double acos(double x);
/**
 * @brief Arc sine of @p x, in radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double asin(double x);
/**
 * @brief Arc tangent of @p x, in radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double atan(double x);
/**
 * @brief Arc tangent of @p y / @p x, using both signs to pick the quadrant.
 *
 * @param[in] y Argument.
 * @param[in] x Argument.
 * @return The result.
 */
double atan2(double y, double x);
/**
 * @brief Smallest integer value not below @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double ceil(double x);
/**
 * @brief Cosine of @p x radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double cos(double x);
/**
 * @brief Hyperbolic cosine of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double cosh(double x);
/**
 * @brief e raised to @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double exp(double x);
/**
 * @brief Largest integer value not above @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double floor(double x);
/**
 * @brief Remainder of @p x / @p y, with the sign of @p x.
 *
 * @param[in] x Argument.
 * @param[in] y Argument.
 * @return The result.
 */
double fmod(double x, double y);
/**
 * @brief Splits @p x into a fraction in [0.5, 1) and a power of two.
 *
 * @param[in] x Argument.
 * @param[out] exponent Receives the power of two.
 * @return The fraction.
 */
double frexp(double x, int *exponent);
/**
 * @brief @p x times 2 raised to @p exponent.
 *
 * @param[in] x Argument.
 * @param[in] exponent Power of two.
 * @return The result.
 */
double ldexp(double x, int exponent);
/**
 * @brief Natural logarithm of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double log(double x);
/**
 * @brief Base-10 logarithm of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double log10(double x);
/**
 * @brief Splits @p x into integer and fractional parts with the sign of @p x.
 *
 * @param[in] x Argument.
 * @param[out] ipart Receives the integer part.
 * @return The fractional part.
 */
double modf(double x, double *ipart);
/**
 * @brief @p x raised to @p y.
 *
 * @param[in] x Argument.
 * @param[in] y Argument.
 * @return The result.
 */
double pow(double x, double y);
/**
 * @brief Sine of @p x radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double sin(double x);
/**
 * @brief Hyperbolic sine of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double sinh(double x);
/**
 * @brief Square root of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double sqrt(double x);
/**
 * @brief Tangent of @p x radians.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double tan(double x);
/**
 * @brief Hyperbolic tangent of @p x.
 *
 * @param[in] x Argument.
 * @return The result.
 */
double tanh(double x);

/**
 * @brief Arc cosine of @p x, in radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double acosl(long double x);
/**
 * @brief Arc sine of @p x, in radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double asinl(long double x);
/**
 * @brief Arc tangent of @p x, in radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double atanl(long double x);
/**
 * @brief Arc tangent of @p y / @p x, using both signs to pick the quadrant. (long double)
 *
 * @param[in] y Argument.
 * @param[in] x Argument.
 * @return The result.
 */
long double atan2l(long double y, long double x);
/**
 * @brief Smallest integer value not below @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double ceill(long double x);
/**
 * @brief Cosine of @p x radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double cosl(long double x);
/**
 * @brief Hyperbolic cosine of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double coshl(long double x);
/**
 * @brief e raised to @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double expl(long double x);
/**
 * @brief Largest integer value not above @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double floorl(long double x);
/**
 * @brief Remainder of @p x / @p y, with the sign of @p x. (long double)
 *
 * @param[in] x Argument.
 * @param[in] y Argument.
 * @return The result.
 */
long double fmodl(long double x, long double y);
/**
 * @brief Splits @p x into a fraction in [0.5, 1) and a power of two. (long double)
 *
 * @param[in] x Argument.
 * @param[out] exponent Receives the power of two.
 * @return The fraction.
 */
long double frexpl(long double x, int *exponent);
/**
 * @brief @p x times 2 raised to @p exponent. (long double)
 *
 * @param[in] x Argument.
 * @param[in] exponent Power of two.
 * @return The result.
 */
long double ldexpl(long double x, int exponent);
/**
 * @brief Natural logarithm of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double logl(long double x);
/**
 * @brief Base-10 logarithm of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double log10l(long double x);
/**
 * @brief Splits @p x into integer and fractional parts with the sign of @p x. (long double)
 *
 * @param[in] x Argument.
 * @param[out] ipart Receives the integer part.
 * @return The fractional part.
 */
long double modfl(long double x, long double *ipart);
/**
 * @brief @p x raised to @p y. (long double)
 *
 * @param[in] x Argument.
 * @param[in] y Argument.
 * @return The result.
 */
long double powl(long double x, long double y);
/**
 * @brief Sine of @p x radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double sinl(long double x);
/**
 * @brief Hyperbolic sine of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double sinhl(long double x);
/**
 * @brief Square root of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double sqrtl(long double x);
/**
 * @brief Tangent of @p x radians. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double tanl(long double x);
/**
 * @brief Hyperbolic tangent of @p x. (long double)
 *
 * @param[in] x Argument.
 * @return The result.
 */
long double tanhl(long double x);

/**
 * @brief Absolute value of @p x.
 *
 * @param[in] x Value.
 * @return |x|.
 */
static inline double fabs(double x) { return __builtin_fabs(x); }
/**
 * @brief Absolute value of @p x.
 *
 * @param[in] x Value.
 * @return |x|.
 */
static inline double __fabs__(double x) { return __builtin_fabs(x); }
/**
 * @brief Absolute value of @p x.
 *
 * @param[in] x Value.
 * @return |x|.
 */
static inline float fabsf(float x) { return __builtin_fabsf(x); }
/**
 * @brief Absolute value of @p x.
 *
 * @param[in] x Value.
 * @return |x|.
 */
static inline long double fabsl(long double x) { return __builtin_fabsl(x); }

#ifdef __cplusplus
}
#endif

#endif