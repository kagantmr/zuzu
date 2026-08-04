// compiler.h - branch/placement hints shared by kernel and userspace.
//
// likely()/unlikely() steer the branch predictor and let the compiler lay
// out the fast path fall-through with the slow path/error path pushed out
// of line. __hot/__cold bias whole-function placement and inlining
// priority the same way, for functions too big to wrap every branch in
// likely()/unlikely() individually. __always_inline forces a call site to
// disappear entirely (right for tiny leaves called from a hot loop, wrong
// for anything with its own loop or non-trivial body). __noinline is the
// opposite: keep a rarely-taken function out-of-line so it doesn't bloat
// the icache footprint of every hot caller that happens to reach it.

#ifndef ZUZU_COMPILER_H
#define ZUZU_COMPILER_H

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define __hot           __attribute__((hot))
#define __cold          __attribute__((cold))
#define __always_inline __attribute__((always_inline)) inline
#define __noinline      __attribute__((noinline))

#endif /* ZUZU_COMPILER_H */
