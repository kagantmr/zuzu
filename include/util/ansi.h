/**
 * @file
 * @brief A small ECMA-48 parser: splits a byte stream into printable characters, C0 controls and
 * CSI sequences. OSC strings are skipped.
 */

#ifndef UTIL_ANSI_H
#define UTIL_ANSI_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Most numeric parameters kept for one CSI sequence; extra ones are dropped. */
#define ANSI_MAX_PARAMS 8

/** @brief Callbacks the parser calls as it recognizes input. */
typedef struct {
    void (*print)(void *ctx, uint8_t ch);   /**< A printable byte. */
    void (*control)(void *ctx, uint8_t ch); /**< A C0 control byte or DEL. */
    void (*csi)(void *ctx, char prefix, char final, const uint32_t *params,
                uint32_t count); /**< A finished CSI sequence; @c prefix is a private marker or 0. */
} AnsiHandlers;

/** @brief Where the parser is inside an escape sequence. */
typedef enum {
    ANSI_GROUND, /**< Plain text. */
    ANSI_ESC,    /**< After ESC. */
    ANSI_CSI,    /**< Inside ESC [ ... */
    ANSI_OSC     /**< Inside ESC ] ..., skipped up to BEL. */
} AnsiState;

/** @brief Parser state. */
typedef struct {
    const AnsiHandlers *handlers;     /**< Callbacks. */
    void *ctx;                        /**< Passed to every callback. */
    AnsiState state;                  /**< Current state. */
    uint32_t params[ANSI_MAX_PARAMS]; /**< Parameters of the CSI sequence being read. */
    uint32_t count;                   /**< Parameters stored so far. */
    uint32_t cur;                     /**< Parameter being read. */
    bool have_cur;                    /**< @c cur has at least one digit. */
    char prefix;                      /**< Private marker ('?', '>', ...) or 0. */
} Ansi;

/**
 * @brief Resets a parser.
 *
 * @param[out] a        The parser.
 * @param[in]  handlers Callbacks; must outlive the parser.
 * @param[in]  ctx      Passed to every callback.
 */
void AnsiInit(Ansi *a, const AnsiHandlers *handlers, void *ctx);

/**
 * @brief Feeds one byte.
 *
 * @param[in,out] a The parser.
 * @param[in]     b The byte.
 */
void AnsiFeed(Ansi *a, uint8_t b);

#endif /* UTIL_ANSI_H */
