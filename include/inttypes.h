/**
 * @file
 * @brief printf format macros for the fixed-width integer types.
 *
 * Only the PRI macros are provided, for a 32-bit ARM target where int32_t is long and int64_t is
 * long long.
 */

#ifndef ZUZU_INTTYPES_H
#define ZUZU_INTTYPES_H

#include <stdint.h>

/** @brief printf conversion for a 8-bit integer, as signed decimal. */
#define PRId8 "d"
/** @brief printf conversion for a 8-bit integer, as signed integer. */
#define PRIi8 "i"
/** @brief printf conversion for a 8-bit integer, as octal. */
#define PRIo8 "o"
/** @brief printf conversion for a 8-bit integer, as unsigned decimal. */
#define PRIu8 "u"
/** @brief printf conversion for a 8-bit integer, as lowercase hexadecimal. */
#define PRIx8 "x"
/** @brief printf conversion for a 8-bit integer, as uppercase hexadecimal. */
#define PRIX8 "X"
/** @brief printf conversion for the fast 8-bit integer type, as signed decimal. */
#define PRIdFAST8 PRId8
/** @brief printf conversion for the fast 8-bit integer type, as signed integer. */
#define PRIiFAST8 PRIi8
/** @brief printf conversion for the fast 8-bit integer type, as octal. */
#define PRIoFAST8 PRIo8
/** @brief printf conversion for the fast 8-bit integer type, as unsigned decimal. */
#define PRIuFAST8 PRIu8
/** @brief printf conversion for the fast 8-bit integer type, as lowercase hexadecimal. */
#define PRIxFAST8 PRIx8
/** @brief printf conversion for the fast 8-bit integer type, as uppercase hexadecimal. */
#define PRIXFAST8 PRIX8
/** @brief printf conversion for the least 8-bit integer type, as signed decimal. */
#define PRIdLEAST8 PRId8
/** @brief printf conversion for the least 8-bit integer type, as signed integer. */
#define PRIiLEAST8 PRIi8
/** @brief printf conversion for the least 8-bit integer type, as octal. */
#define PRIoLEAST8 PRIo8
/** @brief printf conversion for the least 8-bit integer type, as unsigned decimal. */
#define PRIuLEAST8 PRIu8
/** @brief printf conversion for the least 8-bit integer type, as lowercase hexadecimal. */
#define PRIxLEAST8 PRIx8
/** @brief printf conversion for the least 8-bit integer type, as uppercase hexadecimal. */
#define PRIXLEAST8 PRIX8
/** @brief printf conversion for a 16-bit integer, as signed decimal. */
#define PRId16 "d"
/** @brief printf conversion for a 16-bit integer, as signed integer. */
#define PRIi16 "i"
/** @brief printf conversion for a 16-bit integer, as octal. */
#define PRIo16 "o"
/** @brief printf conversion for a 16-bit integer, as unsigned decimal. */
#define PRIu16 "u"
/** @brief printf conversion for a 16-bit integer, as lowercase hexadecimal. */
#define PRIx16 "x"
/** @brief printf conversion for a 16-bit integer, as uppercase hexadecimal. */
#define PRIX16 "X"
/** @brief printf conversion for the fast 16-bit integer type, as signed decimal. */
#define PRIdFAST16 PRId16
/** @brief printf conversion for the fast 16-bit integer type, as signed integer. */
#define PRIiFAST16 PRIi16
/** @brief printf conversion for the fast 16-bit integer type, as octal. */
#define PRIoFAST16 PRIo16
/** @brief printf conversion for the fast 16-bit integer type, as unsigned decimal. */
#define PRIuFAST16 PRIu16
/** @brief printf conversion for the fast 16-bit integer type, as lowercase hexadecimal. */
#define PRIxFAST16 PRIx16
/** @brief printf conversion for the fast 16-bit integer type, as uppercase hexadecimal. */
#define PRIXFAST16 PRIX16
/** @brief printf conversion for the least 16-bit integer type, as signed decimal. */
#define PRIdLEAST16 PRId16
/** @brief printf conversion for the least 16-bit integer type, as signed integer. */
#define PRIiLEAST16 PRIi16
/** @brief printf conversion for the least 16-bit integer type, as octal. */
#define PRIoLEAST16 PRIo16
/** @brief printf conversion for the least 16-bit integer type, as unsigned decimal. */
#define PRIuLEAST16 PRIu16
/** @brief printf conversion for the least 16-bit integer type, as lowercase hexadecimal. */
#define PRIxLEAST16 PRIx16
/** @brief printf conversion for the least 16-bit integer type, as uppercase hexadecimal. */
#define PRIXLEAST16 PRIX16
/** @brief printf conversion for a 32-bit integer, as signed decimal. */
#define PRId32 "ld"
/** @brief printf conversion for a 32-bit integer, as signed integer. */
#define PRIi32 "li"
/** @brief printf conversion for a 32-bit integer, as octal. */
#define PRIo32 "lo"
/** @brief printf conversion for a 32-bit integer, as unsigned decimal. */
#define PRIu32 "lu"
/** @brief printf conversion for a 32-bit integer, as lowercase hexadecimal. */
#define PRIx32 "lx"
/** @brief printf conversion for a 32-bit integer, as uppercase hexadecimal. */
#define PRIX32 "lX"
/** @brief printf conversion for the fast 32-bit integer type, as signed decimal. */
#define PRIdFAST32 PRId32
/** @brief printf conversion for the fast 32-bit integer type, as signed integer. */
#define PRIiFAST32 PRIi32
/** @brief printf conversion for the fast 32-bit integer type, as octal. */
#define PRIoFAST32 PRIo32
/** @brief printf conversion for the fast 32-bit integer type, as unsigned decimal. */
#define PRIuFAST32 PRIu32
/** @brief printf conversion for the fast 32-bit integer type, as lowercase hexadecimal. */
#define PRIxFAST32 PRIx32
/** @brief printf conversion for the fast 32-bit integer type, as uppercase hexadecimal. */
#define PRIXFAST32 PRIX32
/** @brief printf conversion for the least 32-bit integer type, as signed decimal. */
#define PRIdLEAST32 PRId32
/** @brief printf conversion for the least 32-bit integer type, as signed integer. */
#define PRIiLEAST32 PRIi32
/** @brief printf conversion for the least 32-bit integer type, as octal. */
#define PRIoLEAST32 PRIo32
/** @brief printf conversion for the least 32-bit integer type, as unsigned decimal. */
#define PRIuLEAST32 PRIu32
/** @brief printf conversion for the least 32-bit integer type, as lowercase hexadecimal. */
#define PRIxLEAST32 PRIx32
/** @brief printf conversion for the least 32-bit integer type, as uppercase hexadecimal. */
#define PRIXLEAST32 PRIX32
/** @brief printf conversion for a 64-bit integer, as signed decimal. */
#define PRId64 "lld"
/** @brief printf conversion for a 64-bit integer, as signed integer. */
#define PRIi64 "lli"
/** @brief printf conversion for a 64-bit integer, as octal. */
#define PRIo64 "llo"
/** @brief printf conversion for a 64-bit integer, as unsigned decimal. */
#define PRIu64 "llu"
/** @brief printf conversion for a 64-bit integer, as lowercase hexadecimal. */
#define PRIx64 "llx"
/** @brief printf conversion for a 64-bit integer, as uppercase hexadecimal. */
#define PRIX64 "llX"
/** @brief printf conversion for the fast 64-bit integer type, as signed decimal. */
#define PRIdFAST64 PRId64
/** @brief printf conversion for the fast 64-bit integer type, as signed integer. */
#define PRIiFAST64 PRIi64
/** @brief printf conversion for the fast 64-bit integer type, as octal. */
#define PRIoFAST64 PRIo64
/** @brief printf conversion for the fast 64-bit integer type, as unsigned decimal. */
#define PRIuFAST64 PRIu64
/** @brief printf conversion for the fast 64-bit integer type, as lowercase hexadecimal. */
#define PRIxFAST64 PRIx64
/** @brief printf conversion for the fast 64-bit integer type, as uppercase hexadecimal. */
#define PRIXFAST64 PRIX64
/** @brief printf conversion for the least 64-bit integer type, as signed decimal. */
#define PRIdLEAST64 PRId64
/** @brief printf conversion for the least 64-bit integer type, as signed integer. */
#define PRIiLEAST64 PRIi64
/** @brief printf conversion for the least 64-bit integer type, as octal. */
#define PRIoLEAST64 PRIo64
/** @brief printf conversion for the least 64-bit integer type, as unsigned decimal. */
#define PRIuLEAST64 PRIu64
/** @brief printf conversion for the least 64-bit integer type, as lowercase hexadecimal. */
#define PRIxLEAST64 PRIx64
/** @brief printf conversion for the least 64-bit integer type, as uppercase hexadecimal. */
#define PRIXLEAST64 PRIX64
/** @brief printf conversion for intmax_t or uintmax_t, as signed decimal. */
#define PRIdMAX PRId64
/** @brief printf conversion for intptr_t or uintptr_t, as signed decimal. */
#define PRIdPTR "d"
/** @brief printf conversion for intmax_t or uintmax_t, as signed integer. */
#define PRIiMAX PRIi64
/** @brief printf conversion for intptr_t or uintptr_t, as signed integer. */
#define PRIiPTR "i"
/** @brief printf conversion for intmax_t or uintmax_t, as octal. */
#define PRIoMAX PRIo64
/** @brief printf conversion for intptr_t or uintptr_t, as octal. */
#define PRIoPTR "o"
/** @brief printf conversion for intmax_t or uintmax_t, as unsigned decimal. */
#define PRIuMAX PRIu64
/** @brief printf conversion for intptr_t or uintptr_t, as unsigned decimal. */
#define PRIuPTR "u"
/** @brief printf conversion for intmax_t or uintmax_t, as lowercase hexadecimal. */
#define PRIxMAX PRIx64
/** @brief printf conversion for intptr_t or uintptr_t, as lowercase hexadecimal. */
#define PRIxPTR "x"
/** @brief printf conversion for intmax_t or uintmax_t, as uppercase hexadecimal. */
#define PRIXMAX PRIX64
/** @brief printf conversion for intptr_t or uintptr_t, as uppercase hexadecimal. */
#define PRIXPTR "X"

#endif /* ZUZU_INTTYPES_H */
