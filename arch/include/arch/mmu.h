#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include "kernel/mm/vmm/vmm.h"
#include <stdbool.h>
#include <stdint.h>

#define SECTION_SIZE 0x100000u

uintptr_t ArchMmuCreateTables(AddressSpaceType type);

void ArchMmuFreeTables(uintptr_t ttbr_pa, AddressSpaceType type);

bool ArchMmuMap(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                VirtMemType memtype);

bool ArchMmuUnmap(AddressSpace *as, uintptr_t va, size_t size, bool flush);

bool ArchMmuProtect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot);

void ArchMmuEnable(AddressSpace *as);

void ArchMmuSwitch(AddressSpace *as);

void ArchMmuFlushTlb(void);

void ArchMmuFlushTlbAsid(uint8_t asid);

void ArchMmuFlushTlbVa(uintptr_t va);

void ArchMmuFlushTlbVaAsid(uintptr_t va, uint8_t asid);

uintptr_t ArchMmuTranslate(uintptr_t ttbr_pa, uintptr_t va);

bool ArchMmuUnmapPage(AddressSpace *as, uintptr_t va);

void ArchMmuFreeUserPages(AddressSpace *as);

void ArchMmuInitTtbr1(AddressSpace *as);

#include <arch_impl/mmu.h>

#endif // ARCH_MMU_H
