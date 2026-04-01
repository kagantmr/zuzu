# Contributing to zuzu

Thanks for contributing to zuzu!

This document is a practical starting point for code, docs, and design contributions.

## Two contribution paths
zuzu is a microkernel, so contribution paths may look vastly different.

### Userspace path (default)
- Preferred path for most contributions
- Covers daemons, protocol handling, service policy, shell/tools, and most iteration work
- Typical folders:
- `user/`
- `include/zuzu/protocols/`
- `docs/`

### Kernelspace path (high bar)
- Kernel changes are expected to be rare and deliberate.
- Kernel architecture ports are exempt from this rule, but please reach out before starting a new port to discuss the design and scope.
- Typical folders:
- `kernel/`
- `arch/`
- `core/`

Kernelspace PRs should include a short justification explaining why this cannot remain a userspace policy change.

## Project principles
- Keep the kernel minimal and policy-free where possible
- Prefer userspace policy and capability mediation over kernel special cases
- Keep changes understandable and easy to debug on QEMU

## Prerequisites
- ARM embedded toolchain (`arm-none-eabi-*`)
- QEMU system emulator (`qemu-system-arm`)
- `make`

See README for platform-specific install commands.

## Build and run
Build:
- `make -j4`

Run:
- `make run`

## Debugging
Use the debug target and connect GDB as described in README.

## Branch and commit guidance
- Keep commits as small as possible
- One logical change per commit
- Write clear commit messages

Preferred branch naming:
- userspace/<topic>
- kernel/<topic>

Commit message style:
- changetype(subsystem): short summary
- body explaining why and impact

Example:
- refactor(ipc): enforce endpoint handle validation in recv path

## Code style guidance
- Match existing style in each file
- Keep functions short and single-purpose
- Prefer explicit error paths and return codes
- Avoid unrelated refactors in the same patch

## Kernel and userspace boundaries
When in doubt:
- Kernel owns mechanisms (scheduling, memory, IPC primitives)
- Userspace owns policy (service naming, authorization, daemon behavior)

For security-sensitive service access:
- Prefer userspace mediation by trusted daemons
- Avoid hardcoding service policy in kernel paths

Decision rule:
- If it can be enforced in userspace with existing IPC/handle primitives, keep it in userspace.

## Testing checklist

### For userspace PRs
- Build succeeds: `make -j4`
- Boot still works in QEMU
- Changed daemon path is exercised manually
- No obvious regression in related userspace services

If your userspace change touches IPC, include at least:
- successful path test
- failure path test (bad handle, no permission, not found)

### For kernelspace PRs
- All userspace checks above
- Boot + scheduler sanity after repeated runs
- Fault path coverage for changed logic (invalid handle/pointer/permission paths)
- One regression note listing subsystems at risk

If your kernel change touches one of these, include focused evidence:
- IPC dispatch/validation
- scheduler/context switching
- memory mapping and address-space transitions
- exception or IRQ entry paths

## Documentation expectations
If behavior changes, update docs in the same PR:
- README when user-visible workflow changes
- docs files for architecture and protocol updates

## Pull request checklist
- Problem statement and motivation
- Summary of approach
- Risks and limitations
- Test notes
- Follow-up items if intentionally deferred

Required extra fields for kernelspace PRs:
- Why userspace is insufficient
- Invariants preserved (what must remain true)
- Rollback plan if regression appears

## Good first contribution ideas
- Improve diagnostics around syscall and IPC errors
- Add small targeted tests for userspace daemons
- Tighten docs around boot, IPC, and memory behavior
- Fix broken doc links and missing docs files

Not a good first contribution:
- Kernel scheduler, MMU, exception entry, or syscall ABI refactors

## Questions and design proposals
For larger changes:
- Write a short design note first
- Include alternatives considered
- Call out kernel/userspace boundary decisions

A suggested template is:
- context
- goals
- non-goals
- design
- migration plan
- risks
