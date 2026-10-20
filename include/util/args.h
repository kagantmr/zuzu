/**
 * @file
 * @brief Small command-line argument helpers.
 */

#ifndef ZUZU_ARGS_H
#define ZUZU_ARGS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One accepted option; an array of these ends with an entry whose @c name is NULL. */
typedef struct {
    const char *name; /**< Long name without dashes, e.g. "verbose". */
    char shortname;   /**< Short flag, e.g. 'v'; 0 if none. */
    int has_arg;      /**< 0 for a flag, 1 if the option takes a value. */
    const char *help; /**< One-line description for ArgsUsage(). */
} ArgSpec;

/** @brief Result of ArgsParse(). */
typedef struct {
    int argc;                    /**< Argument count, including the program name. */
    char **argv;                 /**< The argument strings. */
    const char *positionals[16]; /**< Arguments that are not options or option values; at most 16 kept. */
    int positional_count;        /**< Number of entries in @c positionals. */
} Args;

/**
 * @brief Collects the positional arguments, skipping options and their values.
 *
 * Options are recognised (so their values are not taken as positionals) but
 * not stored; query them with ArgsHas() / ArgsGet(). Unknown long options
 * are ignored.
 *
 * @param[out] out  Filled with argc/argv and the positionals.
 * @param[in]  argc Argument count.
 * @param[in]  argv Argument strings.
 * @param[in]  spec Accepted options, ending with a NULL @c name.
 * @return 0, or -1 if an option that takes a value is the last argument.
 */
int ArgsParse(Args *out, int argc, char **argv, const ArgSpec *spec);

/**
 * @brief Tells whether an argument string appears on the command line.
 *
 * Compares whole argv entries, so pass the option as typed, e.g. "--verbose" or "-v".
 *
 * @param[in] args Parsed arguments.
 * @param[in] name Exact argument string to look for.
 * @return 1 if present, 0 otherwise.
 */
int ArgsHas(const Args *args, const char *name);

/**
 * @brief Returns the argument that follows @p name on the command line.
 *
 * Compares whole argv entries, so pass the option as typed, e.g. "--out".
 *
 * @param[in] args Parsed arguments.
 * @param[in] name Exact argument string to look for.
 * @return The next argument, or NULL if @p name is absent or last.
 */
const char *ArgsGet(const Args *args, const char *name);

/**
 * @brief Prints a usage line and one line per option to stdout.
 *
 * @param[in] progname Program name, usually argv[0].
 * @param[in] spec     Accepted options, ending with a NULL @c name.
 */
void ArgsUsage(const char *progname, const ArgSpec *spec);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ARGS_H