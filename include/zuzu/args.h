#ifndef ZUZU_ARGS_H
#define ZUZU_ARGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char  *name;      // long name, e.g. "verbose"
    char         shortname; // short flag, e.g. 'v', 0 if none
    int          has_arg;   // 0 = flag, 1 = required arg
    const char  *help; // help string for usage
} ArgSpec;

typedef struct {
    int          argc; // number of arguments (including program name)
    char       **argv; // array of argument strings
    const char  *positionals[16]; // array of positional arguments (up to 16)
    int          positional_count; // number of positional arguments
} Args;

/**
 * @brief Parses command-line arguments based on the provided specification.
 * 
 * @param out Pointer to an Args structure that will be filled with the parsed arguments.
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @param spec The specification of expected arguments, including their names, short flags, and whether they require an argument.
 * 
 * @return int Returns 0 on success, or a negative value on error (e.g., unknown argument, missing required argument).
 */
int  ArgsParse(Args *out, int argc, char **argv, const ArgSpec *spec);

/**
 * @brief Checks if a specific argument is present in the parsed arguments.
 * 
 * @param args Pointer to the parsed Args structure.
 * @param name The name of the argument to check for (long name).
 * 
 * @return int Returns 1 if the argument is present, 0 otherwise.
 */
int  ArgsHas(const Args *args, const char *name);

/**
 * @brief Retrieves the value of a specific argument from the parsed arguments.
 * 
 * @param args Pointer to the parsed Args structure.
 * @param name The name of the argument to retrieve the value for (long name).
 * 
 * @return const char* Returns the value of the argument if present, or NULL if the argument is not found or does not have a value.
 */
const char *ArgsGet(const Args *args, const char *name);

/**
 * @brief Prints the usage information for the program based on the provided argument specification.
 * 
 * @param progname The name of the program (typically argv[0]).
 * @param spec The specification of expected arguments, including their names, short flags, and help strings.
 */
void ArgsUsage(const char *progname, const ArgSpec *spec);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ARGS_H