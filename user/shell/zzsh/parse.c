/**
 * @file parse.c
 *
 * @brief Reading a command line: words, quotes, variables, `~`, wildcards, pipes and redirects.
 */

#include "zzsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/path.h>

/** @brief Room for the expanded words of one pipeline. */
#define POOL_SIZE 4096
/** @brief Wildcard matches taken for one word. */
#define GLOB_MAX 64

static char g_pool[POOL_SIZE]; /**< Where expanded words live until the next pipeline. */
static size_t g_pool_pos;      /**< Bytes of @c g_pool in use. */

/**
 * @brief Reserves @p n bytes in the word pool.
 *
 * @param[in] n Bytes wanted.
 * @return The bytes, or NULL if the pool is full.
 */
static char *PoolAlloc(size_t n)
{
    if (g_pool_pos + n > POOL_SIZE)
        return NULL;
    char *p = g_pool + g_pool_pos;
    g_pool_pos += n;
    return p;
}

/** @brief A word being built. */
typedef struct {
    char text[LINE_BUFFER_SIZE]; /**< The characters so far. */
    size_t len;                  /**< How many. */
    bool any;                    /**< Whether the word exists at all (an empty "" still does). */
    bool glob;                   /**< Whether it has an unquoted `*` or `?`. */
    bool overflow;               /**< Whether it ran out of room. */
} Word;

/**
 * @brief Appends characters to a word.
 *
 * @param[in,out] w The word.
 * @param[in]     s The characters.
 * @param[in]     n How many.
 */
static void WordAdd(Word *w, const char *s, size_t n)
{
    if (w->len + n >= sizeof(w->text)) {
        w->overflow = true;
        return;
    }
    memcpy(w->text + w->len, s, n);
    w->len += n;
}

/**
 * @brief Whether @p c can start or continue a variable name.
 *
 * @param[in] c      The character.
 * @param[in] first  Whether it would be the first one.
 * @return true if it is allowed.
 */
static bool NameChar(char c, bool first)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           (!first && c >= '0' && c <= '9');
}

/**
 * @brief Expands a `$` at @p *p and appends the result to the word.
 *
 * @param[in,out] p The position of the `$`; left after the reference.
 * @param[in,out] w The word.
 */
static void ExpandDollar(const char **p, Word *w)
{
    const char *s = *p + 1;
    char num[16];

    if (*s == '?') {
        WordAdd(w, num, (size_t)snprintf(num, sizeof(num), "%d", g_status));
        *p = s + 1;
    } else if (*s == '$') {
        WordAdd(w, "1", 1); /* there is one shell and no process ids to speak of */
        *p = s + 1;
    } else if (*s == '0') {
        WordAdd(w, "zzsh", 4);
        *p = s + 1;
    } else if (*s == '{') {
        const char *end = strchr(s, '}');
        if (!end || !VarNameOk(s + 1, (size_t)(end - s - 1))) {
            WordAdd(w, "$", 1);
            *p = s;
            return;
        }
        char name[VAR_NAME_MAX];
        memcpy(name, s + 1, (size_t)(end - s - 1));
        name[end - s - 1] = '\0';
        const char *v = VarGet(name);
        if (v)
            WordAdd(w, v, strlen(v));
        *p = end + 1;
    } else if (NameChar(*s, true)) {
        char name[VAR_NAME_MAX];
        size_t n = 0;
        while (NameChar(s[n], n == 0) && n < VAR_NAME_MAX - 1) {
            name[n] = s[n];
            n++;
        }
        name[n] = '\0';
        const char *v = VarGet(name);
        if (v)
            WordAdd(w, v, strlen(v));
        *p = s + n;
    } else {
        WordAdd(w, "$", 1);
        *p = s;
    }
}

/**
 * @brief Reads one word: quotes, escapes, `$` and a leading `~` are handled here.
 *
 * @param[in,out] p The position; left at the first character after the word.
 * @param[out]    w The word.
 * @return false on a syntax error (an unterminated quote), after printing it.
 */
static bool ReadWord(const char **p, Word *w)
{
    const char *s = *p;
    *w = (Word){0};

    if (*s == '~' && (s[1] == '\0' || s[1] == '/' || s[1] == ' ' || s[1] == '\t')) {
        const char *home = VarGet("HOME");
        WordAdd(w, home ? home : "/", home ? strlen(home) : 1);
        w->any = true;
        s++;
    }

    while (*s && *s != ' ' && *s != '\t' && *s != '|' && *s != '&' && *s != ';' && *s != '<' &&
           *s != '>') {
        w->any = true;
        if (*s == '\'') {
            const char *end = strchr(s + 1, '\'');
            if (!end) {
                printf("zzsh: syntax error: unterminated '\n");
                return false;
            }
            WordAdd(w, s + 1, (size_t)(end - s - 1));
            s = end + 1;
        } else if (*s == '"') {
            s++;
            while (*s && *s != '"') {
                if (*s == '\\' && (s[1] == '"' || s[1] == '\\' || s[1] == '$')) {
                    WordAdd(w, s + 1, 1);
                    s += 2;
                } else if (*s == '$') {
                    ExpandDollar(&s, w);
                } else {
                    WordAdd(w, s++, 1);
                }
            }
            if (*s != '"') {
                printf("zzsh: syntax error: unterminated \"\n");
                return false;
            }
            s++;
        } else if (*s == '\\') {
            if (s[1])
                WordAdd(w, s + 1, 1);
            s += s[1] ? 2 : 1;
        } else if (*s == '$') {
            ExpandDollar(&s, w);
        } else {
            if (*s == '*' || *s == '?')
                w->glob = true;
            WordAdd(w, s++, 1);
        }
    }
    *p = s;
    if (w->overflow) {
        printf("zzsh: word too long\n");
        return false;
    }
    w->text[w->len] = '\0';
    return true;
}

/**
 * @brief Whether a name matches a pattern with `*` and `?`.
 *
 * @param[in] pat  The pattern.
 * @param[in] name The name.
 * @return true on a match.
 */
static bool Match(const char *pat, const char *name)
{
    if (*pat == '\0')
        return *name == '\0';
    if (*pat == '*') {
        for (const char *n = name;; n++) {
            if (Match(pat + 1, n))
                return true;
            if (!*n)
                return false;
        }
    }
    if (*name && (*pat == '?' || *pat == *name))
        return Match(pat + 1, name + 1);
    return false;
}

/**
 * @brief Expands a wildcard word against the directory it names.
 *
 * @param[in]  word The word, with at least one unquoted wildcard.
 * @param[out] out  The matches, in the word pool.
 * @param[in]  max  Room in @p out.
 * @return The number of matches; 0 means keep the word as it is.
 */
static int Glob(const char *word, char **out, int max)
{
    const char *slash = strrchr(word, '/');
    const char *pat = slash ? slash + 1 : word;
    char dir[PATH_MAX_LEN];
    const char *dir_arg = ".";

    if (slash) {
        size_t dl = (size_t)(slash - word);
        if (memchr(word, '*', dl) || memchr(word, '?', dl))
            return 0; /* wildcards in a directory part are not supported */
        if (dl == 0) {
            dir_arg = "/";
        } else {
            if (dl >= sizeof(dir))
                return 0;
            memcpy(dir, word, dl);
            dir[dl] = '\0';
            dir_arg = dir;
        }
    }

    char abs[PATH_MAX_LEN];
    if (!ShEnsureFsd() || !PathResolve(g_cwd, dir_arg, abs, sizeof(abs)))
        return 0;

    int n = 0;
    FsdDirEntry ents[8];
    size_t prefix = slash ? (size_t)(slash - word) + 1 : 0;
    for (uint32_t start = 0;; start += 8) {
        uint32_t got = 0;
        if (FsdReadDir(ShFsd(), abs, start, ents, 8, &got) != ZUZU_OK || got == 0)
            break;
        for (uint32_t i = 0; i < got && n < max; i++) {
            if (ents[i].name[0] == '.' && pat[0] != '.')
                continue;
            if (!Match(pat, ents[i].name))
                continue;
            char *m = PoolAlloc(prefix + strlen(ents[i].name) + 1);
            if (!m)
                return n;
            memcpy(m, word, prefix);
            strcpy(m + prefix, ents[i].name);
            out[n++] = m;
        }
        if (got < 8)
            break;
    }
    return n;
}

/**
 * @brief Whether a word reads as NAME=value.
 *
 * @param[in] w The expanded word.
 * @return true if it is an assignment.
 */
static bool IsAssign(const char *w)
{
    const char *eq = strchr(w, '=');
    return eq && eq != w && VarNameOk(w, (size_t)(eq - w));
}

/**
 * @brief Whether a command has anything in it yet.
 *
 * @param[in] c The command.
 * @return true if it has a word, an assignment or a redirect.
 */
static bool HasContent(const ShCmd *c) { return c->argc > 0 || c->nassign > 0 || c->in || c->out; }

/**
 * @brief Copies a word into the pool.
 *
 * @param[in] w The word.
 * @return The copy, or NULL if the pool is full.
 */
static char *Store(const Word *w)
{
    char *s = PoolAlloc(w->len + 1);
    if (s)
        memcpy(s, w->text, w->len + 1);
    return s;
}

/**
 * @brief Reports a syntax error and moves past the rest of the line.
 *
 * @param[out] cursor Set to the end of the line.
 * @param[in]  p      Where parsing stopped.
 * @param[in]  what   What was wrong.
 * @return PARSE_ERROR.
 */
static ShParse Fail(const char **cursor, const char *p, const char *what)
{
    printf("zzsh: syntax error: %s\n", what);
    *cursor = p + strlen(p);
    return PARSE_ERROR;
}

ShParse ParsePipeline(const char **cursor, ShPipeline *pl, ShSep *sep)
{
    g_pool_pos = 0;
    memset(pl, 0, sizeof(*pl));
    *sep = SEP_NONE;
    ShCmd *c = &pl->cmd[0];
    const char *p = *cursor;

    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '#') {
            p += strlen(p);
            break;
        }
        if (*p == ';') {
            *sep = SEP_SEMI;
            p++;
            break;
        }
        if (p[0] == '&' && p[1] == '&') {
            *sep = SEP_AND;
            p += 2;
            break;
        }
        if (p[0] == '|' && p[1] == '|') {
            *sep = SEP_OR;
            p += 2;
            break;
        }
        if (*p == '&')
            return Fail(cursor, p, "background jobs are not supported");
        if (*p == '|') {
            if (!HasContent(c))
                return Fail(cursor, p, "nothing before '|'");
            if (pl->n + 1 >= SH_MAX_STAGES)
                return Fail(cursor, p, "too many stages in a pipeline");
            c = &pl->cmd[++pl->n];
            p++;
            continue;
        }

        Word w;
        if (*p == '<' || *p == '>') {
            bool in = *p == '<';
            bool append = false;
            p++;
            if (!in && *p == '>') {
                append = true;
                p++;
            }
            while (*p == ' ' || *p == '\t')
                p++;
            if (!ReadWord(&p, &w))
                return Fail(cursor, p, "bad redirect");
            if (!w.any)
                return Fail(cursor, p, "a redirect needs a file");
            char *file = Store(&w);
            if (!file)
                return Fail(cursor, p, "line too long");
            if (in) {
                c->in = file;
            } else {
                c->out = file;
                c->append = append;
            }
            continue;
        }

        if (!ReadWord(&p, &w))
            return Fail(cursor, p, "bad word");
        if (!w.any)
            continue;

        if (c->argc == 0 && IsAssign(w.text)) {
            if (c->nassign == SH_MAX_ASSIGNS)
                return Fail(cursor, p, "too many assignments");
            char *a = Store(&w);
            if (!a)
                return Fail(cursor, p, "line too long");
            c->assign[c->nassign++] = a;
            continue;
        }

        char *matches[GLOB_MAX];
        int nm = w.glob ? Glob(w.text, matches, GLOB_MAX) : 0;
        if (nm == 0) {
            matches[0] = Store(&w);
            if (!matches[0])
                return Fail(cursor, p, "line too long");
            nm = 1;
        }
        for (int i = 0; i < nm; i++) {
            if (c->argc == SH_MAX_ARGS)
                return Fail(cursor, p, "too many words");
            c->argv[c->argc++] = matches[i];
        }
    }

    *cursor = p;
    if (HasContent(c))
        pl->n++;
    else if (pl->n > 0)
        return Fail(cursor, p, "nothing after '|'");
    return pl->n == 0 ? PARSE_EMPTY : PARSE_OK;
}
