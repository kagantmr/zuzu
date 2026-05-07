#include <zuzu/args.h>
#include <string.h>
#include <mem.h>
#include <stdio.h>

int args_parse(args_t *out, int argc, char **argv, const arg_spec_t *spec) {
    memset(out, 0, sizeof(*out));
    out->argc = argc;
    out->argv = argv;

    for (int i = 1; i < argc; ++i) {
        char *s = argv[i];
        if (s[0] == '-' && s[1] == '-') {
            const char *name = s + 2;
            const arg_spec_t *p = spec;
            int matched = 0;
            while (p && p->name) {
                if (strcmp(p->name, name) == 0) {
                    matched = 1;
                    if (p->has_arg) {
                        if (i + 1 >= argc) return -1;
                        ++i; // skip arg
                    }
                    break;
                }
                ++p;
            }
            if (!matched) {
                // unknown option -> ignore
            }
        } else if (s[0] == '-' && s[1] != '\0') {
            // short flags, handle each char
            for (int j = 1; s[j]; ++j) {
                char c = s[j];
                const arg_spec_t *p = spec;
                while (p && p->name) {
                    if (p->shortname == c) {
                        if (p->has_arg) {
                            if (j != (int)strlen(s) - 1) {
                                // remainder is arg
                                break;
                            } else {
                                if (i + 1 >= argc) return -1;
                                ++i;
                            }
                        }
                        break;
                    }
                    ++p;
                }
            }
        } else {
            if (out->positional_count < (int)(sizeof(out->positionals)/sizeof(out->positionals[0]))) {
                out->positionals[out->positional_count++] = s;
            }
        }
    }
    return 0;
}

int args_has(const args_t *args, const char *name) {
    for (int i = 0; i < args->argc; ++i) {
        if (args->argv[i] && strcmp(args->argv[i], name) == 0)
            return 1;
    }
    return 0;
}

const char *args_get(const args_t *args, const char *name) {
    for (int i = 0; i < args->argc - 1; ++i) {
        if (args->argv[i] && strcmp(args->argv[i], name) == 0)
            return args->argv[i+1];
    }
    return NULL;
}

void args_usage(const char *progname, const arg_spec_t *spec) {
    printf("Usage: %s [options]\n", progname);
    const arg_spec_t *p = spec;
    while (p && p->name) {
        if (p->shortname)
            printf("  -%c, --%s\t%s\n", p->shortname, p->name, p->help);
        else
            printf("      --%s\t%s\n", p->name, p->help);
        ++p;
    }
}
