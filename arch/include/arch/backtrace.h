// arch/backtrace.h - Neutral stack-unwind contract.
//
// Walks the current call stack using whatever frame-chaining convention the
// architecture's calling convention defines (e.g. the AAPCS fp/lr chain on
// ARM). Diagnostics-only (core/panic.c is the only caller); not on any hot
// path.
//
//   size_t arch_backtrace_walk(Register fp, Register kernel_va_base,
//                              uint32_t *out, size_t max_depth);
//     -- collect up to max_depth return addresses into out[], starting from
//        fp, stopping at the first frame that doesn't look like valid
//        kernel stack. Returns the number of addresses written.

#ifndef ZUZU_ARCH_BACKTRACE_H
#define ZUZU_ARCH_BACKTRACE_H

#include <arch/regs.h>
#include <stddef.h>
#include <stdint.h>

#include <arch_impl/backtrace.h> /* arch_backtrace_walk() */

#endif // ZUZU_ARCH_BACKTRACE_H
