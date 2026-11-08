/**
 * @file
 * @brief AArch64 ESR_EL1 exception classes.
 */

#ifndef ARCH_AARCH64_EXCEPTIONS_ESR_H
#define ARCH_AARCH64_EXCEPTIONS_ESR_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Exception class, ESR_EL1.EC (bits 31:26). */
typedef enum {
    EC_UNKNOWN = 0x00,           /**< Unknown reason, including undefined instructions. */
    EC_WFX = 0x01,               /**< Trapped WFI or WFE. */
    EC_FP_SIMD = 0x07,           /**< FP/SIMD access trapped by CPACR_EL1.FPEN. */
    EC_ILLEGAL_STATE = 0x0E,     /**< Illegal execution state. */
    EC_SVC32 = 0x11,             /**< SVC from AArch32. */
    EC_SVC64 = 0x15,             /**< SVC from AArch64. */
    EC_SYSREG = 0x18,            /**< Trapped MSR, MRS or system instruction. */
    EC_IABT_LOWER = 0x20,        /**< Instruction abort from a lower EL. */
    EC_IABT_SAME = 0x21,         /**< Instruction abort from the same EL. */
    EC_PC_ALIGN = 0x22,          /**< PC alignment fault. */
    EC_DABT_LOWER = 0x24,        /**< Data abort from a lower EL. */
    EC_DABT_SAME = 0x25,         /**< Data abort from the same EL. */
    EC_SP_ALIGN = 0x26,          /**< SP alignment fault. */
    EC_FP_EXC32 = 0x28,          /**< FP exception from AArch32. */
    EC_FP_EXC64 = 0x2C,          /**< FP exception from AArch64. */
    EC_SERROR = 0x2F,            /**< SError. */
    EC_BREAKPOINT_LOWER = 0x30,  /**< Breakpoint from a lower EL. */
    EC_BREAKPOINT_SAME = 0x31,   /**< Breakpoint from the same EL. */
    EC_SOFTSTEP_LOWER = 0x32,    /**< Software step from a lower EL. */
    EC_SOFTSTEP_SAME = 0x33,     /**< Software step from the same EL. */
    EC_WATCHPOINT_LOWER = 0x34,  /**< Watchpoint from a lower EL. */
    EC_WATCHPOINT_SAME = 0x35,   /**< Watchpoint from the same EL. */
    EC_BKPT32 = 0x38,            /**< BKPT from AArch32. */
    EC_BRK64 = 0x3C,             /**< BRK from AArch64. */
} EsrClass;

/**
 * @brief Extracts the exception class from an ESR_EL1 value.
 *
 * @param[in] esr ESR_EL1.
 * @return ESR_EL1.EC.
 */
static inline EsrClass EsrEc(uint64_t esr) { return (EsrClass)((esr >> 26) & 0x3F); }

/**
 * @brief Extracts the instruction-specific syndrome from an ESR_EL1 value.
 *
 * @param[in] esr ESR_EL1.
 * @return ESR_EL1.ISS, bits 24:0.
 */
static inline uint32_t EsrIss(uint64_t esr) { return (uint32_t)(esr & 0x1FFFFFF); }

/**
 * @brief Whether an abort's fault status code is a translation fault, at any level.
 *
 * @param[in] iss ESR_EL1.ISS of a data or instruction abort.
 * @return true for DFSC/IFSC 0x04-0x07.
 */
static inline bool EsrIsTranslationFault(uint32_t iss) { return ((iss & 0x3F) >> 2) == 0x1; }

/**
 * @brief Whether a data abort was caused by a write (ISS.WnR).
 *
 * @param[in] iss ESR_EL1.ISS of a data abort.
 * @return true for a write.
 */
static inline bool EsrIsWrite(uint32_t iss) { return (iss & (1U << 6)) != 0; }

/**
 * @brief Names an exception class.
 *
 * @param[in] ec Exception class.
 * @return A static name; "Unknown exception class" for codes zuzu does not list.
 */
const char *EcName(EsrClass ec);

/**
 * @brief Describes the fault status code of a data or instruction abort.
 *
 * @param[in] dfsc DFSC or IFSC field, ESR_EL1.ISS[5:0].
 * @return A static description, with the translation level where the code has one.
 */
const char *DecodeDfsc(uint32_t dfsc);

#endif // ARCH_AARCH64_EXCEPTIONS_ESR_H
