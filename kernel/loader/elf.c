#include "elf.h"
#include <mem.h>

uint32_t elf_validate(const void *data, size_t size) {
    if (size < sizeof(Elf32_Ehdr))
        return 0;
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
    if (memcmp(ehdr->e_ident, ELF_MAGIC, 4) != 0
        || ehdr->e_ident[4] != ELF_CLASS_32
        || ehdr->e_ident[5] != ELF_DATA_LITTLE
        || ehdr->e_machine != ELF_MACHINE_ARM
        || ehdr->e_type != ET_EXEC)
    {
        return 0;
    }
    uint32_t ph_end = ehdr->e_phoff + (uint32_t)ehdr->e_phnum * ehdr->e_phentsize;
    if (ph_end > size)
        return 0;
    return ehdr->e_entry;
}

int elf_phdr_count(const void *data) {
    const Elf32_Ehdr *ehdr = data;
    return ehdr->e_phnum;
}

Elf32_Phdr *elf_phdr_get(const void *data, int index) {
    const Elf32_Ehdr *ehdr = data;
    return (Elf32_Phdr *)((const uint8_t *)data + ehdr->e_phoff) + index;
}