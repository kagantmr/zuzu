/**
 * @file
 * @brief The parts of the ELF32 format the loader reads.
 */

#ifndef ELF_H
#define ELF_H

#include <stddef.h>
#include <stdint.h>

/** @brief The first four bytes of every ELF file. */
#define ELF_MAGIC                                                                                  \
    "\x7f"                                                                                         \
    "ELF"
/** @brief e_ident[4]: 32-bit objects. */
#define ELF_CLASS_32 1
/** @brief e_ident[5]: little-endian. */
#define ELF_DATA_LITTLE 1
/** @brief e_machine: ARM. */
#define ELF_MACHINE_ARM 40
/** @brief e_type: executable. */
#define ET_EXEC 2
/** @brief p_type: loadable segment. */
#define PT_LOAD 1
/** @brief p_flags: executable. */
#define PF_X 0x1
/** @brief p_flags: writable. */
#define PF_W 0x2
/** @brief p_flags: readable. */
#define PF_R 0x4

/** @brief ELF32 file header. */
typedef struct {
    uint8_t e_ident[16];  /**< ELF identification. */
    uint16_t e_type;      /**< Object file type. */
    uint16_t e_machine;   /**< Machine type. */
    uint32_t e_version;   /**< Object file version. */
    uint32_t e_entry;     /**< Entry point address. */
    uint32_t e_phoff;     /**< Program header offset. */
    uint32_t e_shoff;     /**< Section header offset. */
    uint32_t e_flags;     /**< Processor-specific flags. */
    uint16_t e_ehsize;    /**< ELF header size. */
    uint16_t e_phentsize; /**< Size of program header entry. */
    uint16_t e_phnum;     /**< Number of program header entries. */
    uint16_t e_shentsize; /**< Size of section header entry. */
    uint16_t e_shnum;     /**< Number of section header entries. */
    uint16_t e_shstrndx;  /**< Section name string table index. */
} Elf32_Ehdr;

/** @brief ELF32 program header. */
typedef struct {
    uint32_t p_type;   /**< Type of segment. */
    uint32_t p_offset; /**< Offset in file. */
    uint32_t p_vaddr;  /**< Virtual address in memory. */
    uint32_t p_paddr;  /**< Physical address. */
    uint32_t p_filesz; /**< Size of segment in file. */
    uint32_t p_memsz;  /**< Size of segment in memory. */
    uint32_t p_flags;  /**< Segment attributes. */
    uint32_t p_align;  /**< Alignment of segment. */
} Elf32_Phdr;

/**
 * @brief Checks that @p data is a 32-bit little-endian ARM executable whose program headers fit.
 *
 * @param[in] data Pointer to the ELF file data in memory.
 * @param[in] size Size of the ELF file data in bytes.
 * @return The entry point, or 0 if the file is not acceptable.
 */
uint32_t ElfValidate(const void *data, size_t size);

/**
 * @brief Returns the number of program headers in the ELF file.
 *
 * @param[in] data A file that passed ElfValidate().
 * @return Number of program headers.
 */
int ElfPhdrCount(const void *data);

/**
 * @brief Returns a pointer to the program header at the specified index.
 *
 * @param[in] data  A file that passed ElfValidate().
 * @param[in] index Index of the program header, below ElfPhdrCount(); not checked.
 * @return Pointer to the program header, inside @p data.
 */
const Elf32_Phdr *ElfPhdrGet(const void *data, int index);

#endif // ELF_H