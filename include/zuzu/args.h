#ifndef ZUZU_ARGS_H
#define ZUZU_ARGS_H

typedef struct {
    const char  *name;      // long name, e.g. "verbose"
    char         shortname; // short flag, e.g. 'v', 0 if none
    int          has_arg;   // 0 = flag, 1 = required arg
    const char  *help;
} arg_spec_t;

typedef struct {
    int          argc;
    char       **argv;
    const char  *positionals[16];
    int          positional_count;
} args_t;

int  args_parse(args_t *out, int argc, char **argv, const arg_spec_t *spec);
int  args_has(const args_t *args, const char *name);
const char *args_get(const args_t *args, const char *name);
void args_usage(const char *progname, const arg_spec_t *spec);

#endif // ZUZU_ARGS_H