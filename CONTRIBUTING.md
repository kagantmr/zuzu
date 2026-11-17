# Contributing to zuzu and zuzuOS

Thanks for contributing to zuzu & zuzuOS!

## LLM policy

The project accepts LLM-aided contributions, but requires human supervision at all costs. Low-effort and unchecked LLM-generated code will **not** be accepted, especially in the kernel.

## zuzuOS contributions

Contributing to zuzuOS is highly encouraged as zuzuOS is currently at a very early stage. The sections below detail various contribution paths one might prefer.

### Utilities

Shell utilities such as `rmdir`,`ls`,`cat` etc., network shell utilities such as `arp`... When making these, make sure that your application is safe and the underlying service is supported. If not, then you might also want to contribute in Services! 

### Services

Services (or daemons) are long running tasks that serve some functionality like time, networking, filesystem access etc. When making a server, be sure that:

- Your service correctly serves the service it is supposed to, and is secure.
- Your service does not abuse resources.
- Old methods like thread-per-connection may be wasteful, prefer an event loop around zuzu primitives instead.

### Drivers

When writing device drivers, be sure that you are aware of the BSP layer, and how to modify it. Drivers, especially those able to perform DMA, must be written **carefully** as to not put the device or the operating system in a fragile state.

- Prefer an event loop.
- Be especially sure that you are able to guarantee DMA accesses cannot be messed with since without an IOMMU DMA is practically unsafe.

## zuzu contributions

The zuzu ukernel is much more mature than zuzuOS, having more work spent on it. Contribution to zuzu will be strictly checked and verified, and evaluated in terms of need, size and security.

### Architecture ports

Ports of zuzu are being planned for various architectures such as AArch64, RISC-V, x86_64 and PowerPC. To port zuzu to a new architecture, please get in contact first to plan the scope and size of the port.

### Bug fixes

Kernel bug fixes are an important part of zuzu, and it would be appreciated!

## Style

- Code is C11 formatted with the repository's `.clang-format`; run `clang-format` on what you change, not on whole files.
- Functions and types are `PascalCase` (`KSymLookup`, `PanicFaultContext`), variables and fields are `snake_case`, macros are `UPPER_SNAKE_CASE`, and anything architecture-specific lives under `arch/` behind an `Arch`-prefixed function. 
- Code outside `arch/` must stay correct at both 32 and 64 bits: use `uintptr_t` for addresses, never `uint32_t`. 
- A change must compile with no warnings. 
- Every function, type and macro gets a Doxygen `@brief` (`make doc-check` lists what's missing). 
- Commits follow Conventional Commits with a scope, e.g. `fix(irq): ...` or `docs(arch): ...`.
