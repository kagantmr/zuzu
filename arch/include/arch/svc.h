// arch/svc.h - Neutral syscall-invocation contract (userspace side).
//
//   typedef struct { Register r0, r1, r2, r3; } SvcResult;
//   Register  ArchInvokeSvc (uint32_t num, Register a0, Register a1, Register a2, Register a3);
//   SvcResult ArchInvokeSvc4(uint32_t num, Register a0, Register a1, Register a2, Register a3);
//
// ArchInvokeSvc returns only the status word; ArchInvokeSvc4 returns all four
// result registers. `num` travels in a register (the kernel reads it back with
// ArchFrameSvcNum), so it need not be a compile-time constant.

#ifndef ARCH_SVC_H
#define ARCH_SVC_H

#include <arch_impl/svc.h>

#endif // ARCH_SVC_H