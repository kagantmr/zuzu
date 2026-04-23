#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>


#define ELF_MAGIC       "\x7f" "ELF"
#define ELF_CLASS_32    1
#define ELF_DATA_LITTLE 1
#define ELF_MACHINE_ARM 40
#define ET_EXEC         2
#define PT_LOAD         1
#define PF_X            0x1
#define PF_W            0x2
#define PF_R            0x4

typedef struct
{
    uint8_t e_ident[16];  /* ELF identification */
    uint16_t e_type;      /* Object file type */
    uint16_t e_machine;   /* Machine type */
    uint32_t e_version;   /* Object file version */
    uint32_t e_entry;     /* Entry point address */
    uint32_t e_phoff;     /* Program header offset */
    uint32_t e_shoff;     /* Section header offset */
    uint32_t e_flags;     /* Processor-specific flags */
    uint16_t e_ehsize;    /* ELF header size */
    uint16_t e_phentsize; /* Size of program header entry */
    uint16_t e_phnum;     /* Number of program header entries */
    uint16_t e_shentsize; /* Size of section header entry */
    uint16_t e_shnum;     /* Number of section header entries */
    uint16_t e_shstrndx;  /* Section name string table index */
} Elf32_Ehdr;

typedef struct
{
    uint32_t p_type;   /* Type of segment */
    uint32_t p_offset; /* Offset in file */
    uint32_t p_vaddr;  /* Virtual address in memory */
    uint32_t p_paddr;  /* Physical address */
    uint32_t p_filesz; /* Size of segment in file */
    uint32_t p_memsz;  /* Size of segment in memory */
    uint32_t p_flags;  /* Segment attributes */
    uint32_t p_align;  /* Alignment of segment */
} Elf32_Phdr;

// Returns the entry point on success, 0 on failure
uint32_t elf_validate(const void *data, size_t size);

int          elf_phdr_count(const void *data);
Elf32_Phdr  *elf_phdr_get(const void *data, int index);

#endif // ELF_H