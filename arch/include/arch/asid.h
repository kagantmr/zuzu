/**
 * @file asid.h
 * @brief Address Space Identifier (ASID) functionality.
 */

#ifndef ARCH_ASID_H
#define ARCH_ASID_H

#include <stdint.h>

/**
 * @brief on ARMv7, an ASID is 8 bits.
 * 
 * TODO: Make this arch-specific.
 */
typedef uint8_t Asid;

/**
 * @brief Represents an ASID token (with an ASID and its generation.)
 */
typedef struct {
    Asid asid; /**< ASID value. */
    uint32_t generation; /**< Its generation. */
} AsidToken;

/**
 * @brief Allocate a new ASID token.
 * 
 * @return AsidToken
 */
AsidToken AsidAlloc(void);

/**
 * @brief Let go of an existing ASID token.
 *
 * @param[in] token Token from AsidAlloc(); a token from an older generation is ignored.
 */
void AsidFree(AsidToken token);

/**
 * @brief Current global generation value.
 */
extern uint32_t asid_generation;

/**
 * @brief ASID generation accessor.
 * 
 * @return uint32_t the ASID generation.
 */
static inline uint32_t AsidCurrentGeneration(void) { return asid_generation; }

/**
 * @brief Activates the provided ASID by marking it on the ASID bitmap.
 *
 * @param[in] asid ASID now loaded on the CPU.
 */
void AsidSetActive(Asid asid);

#endif // ARCH_ASID_H
