/**
 * @file
 * @brief Branch and placement hints shared by the kernel and userspace.
 */

#ifndef ZUZU_COMPILER_H
#define ZUZU_COMPILER_H

/**
 * @brief Hints that @p x is usually true, so the compiler lays out that path as the fall-through.
 */
#define likely(x) __builtin_expect(!!(x), 1)
/** @brief Hints that @p x is usually false, moving that path out of line. */
#define unlikely(x) __builtin_expect(!!(x), 0)

/** @brief Marks a function as hot: optimized harder and placed with other hot code. */
#define __hot __attribute__((hot))
/** @brief Inlines a function at every call site; for tiny leaves on hot paths. */
#define __always_inline __attribute__((always_inline)) inline

#endif /* ZUZU_COMPILER_H */
