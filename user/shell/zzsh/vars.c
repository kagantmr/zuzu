/**
 * @file vars.c
 *
 * @brief Shell variables and the environment handed to child programs.
 */

#include "zzsh.h"
#include <string.h>

/** @brief One shell variable. */
typedef struct {
    bool used;                  /**< Whether the slot holds a variable. */
    bool exported;              /**< Whether children inherit it. */
    char name[VAR_NAME_MAX];    /**< Its name. */
    char value[VAR_VALUE_MAX];  /**< Its value. */
} Var;

static Var g_vars[VAR_MAX]; /**< The variable table. */

bool VarNameOk(const char *name, size_t len)
{
    if (len == 0 || len >= VAR_NAME_MAX)
        return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        bool digit = c >= '0' && c <= '9';
        if (!(alpha || (digit && i > 0)))
            return false;
    }
    return true;
}

/**
 * @brief Finds a variable.
 *
 * @param[in] name Its name.
 * @return The slot, or NULL.
 */
static Var *Find(const char *name)
{
    for (int i = 0; i < VAR_MAX; i++) {
        if (g_vars[i].used && strcmp(g_vars[i].name, name) == 0)
            return &g_vars[i];
    }
    return NULL;
}

/**
 * @brief Finds a variable, making it if it is not there.
 *
 * @param[in] name Its name.
 * @return The slot, or NULL if the name is bad or the table is full.
 */
static Var *FindOrMake(const char *name)
{
    Var *v = Find(name);
    if (v)
        return v;
    if (!VarNameOk(name, strlen(name)))
        return NULL;
    for (int i = 0; i < VAR_MAX; i++) {
        if (!g_vars[i].used) {
            g_vars[i] = (Var){.used = true};
            strcpy(g_vars[i].name, name);
            return &g_vars[i];
        }
    }
    return NULL;
}

const char *VarGet(const char *name)
{
    Var *v = Find(name);
    return v ? v->value : NULL;
}

bool VarSet(const char *name, const char *value)
{
    if (strlen(value) >= VAR_VALUE_MAX)
        return false;
    Var *v = FindOrMake(name);
    if (!v)
        return false;
    strcpy(v->value, value);
    return true;
}

bool VarExport(const char *name, const char *value)
{
    Var *v = FindOrMake(name);
    if (!v)
        return false;
    if (value) {
        if (strlen(value) >= VAR_VALUE_MAX)
            return false;
        strcpy(v->value, value);
    }
    v->exported = true;
    return true;
}

void VarUnset(const char *name)
{
    Var *v = Find(name);
    if (v)
        *v = (Var){0};
}

int VarSlots(void) { return VAR_MAX; }

bool VarAt(int i, const char **name, const char **value, bool *exported)
{
    if (i < 0 || i >= VAR_MAX || !g_vars[i].used)
        return false;
    *name = g_vars[i].name;
    *value = g_vars[i].value;
    *exported = g_vars[i].exported;
    return true;
}

/**
 * @brief Appends "NAME=value" and a terminator to a buffer.
 *
 * @param[in,out] buf   The buffer.
 * @param[in]     cap   Its size.
 * @param[in,out] len   Bytes used.
 * @param[in]     name  Name part.
 * @param[in]     nlen  Length of the name part.
 * @param[in]     value Value part.
 * @return false if it does not fit.
 */
static bool Append(char *buf, size_t cap, size_t *len, const char *name, size_t nlen,
                   const char *value)
{
    size_t vlen = strlen(value);
    if (*len + nlen + 1 + vlen + 1 > cap)
        return false;
    memcpy(buf + *len, name, nlen);
    *len += nlen;
    buf[(*len)++] = '=';
    memcpy(buf + *len, value, vlen + 1);
    *len += vlen + 1;
    return true;
}

bool VarBuildEnv(char *buf, size_t cap, size_t *len, uint32_t *count, char *const *extra,
                 int nextra)
{
    *len = 0;
    *count = 0;
    for (int i = 0; i < VAR_MAX; i++) {
        if (!g_vars[i].used || !g_vars[i].exported)
            continue;
        /* A per-command assignment of the same name replaces it. */
        bool overridden = false;
        size_t nl = strlen(g_vars[i].name);
        for (int e = 0; e < nextra; e++) {
            if (strncmp(extra[e], g_vars[i].name, nl) == 0 && extra[e][nl] == '=')
                overridden = true;
        }
        if (overridden)
            continue;
        if (!Append(buf, cap, len, g_vars[i].name, nl, g_vars[i].value))
            return false;
        (*count)++;
    }
    for (int e = 0; e < nextra; e++) {
        const char *eq = strchr(extra[e], '=');
        if (!eq || !Append(buf, cap, len, extra[e], (size_t)(eq - extra[e]), eq + 1))
            return false;
        (*count)++;
    }
    return true;
}
