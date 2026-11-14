/**
 * @file edit.c
 *
 * @brief Reading a line: cursor editing, history, completion and the prompt.
 */

#include "zzsh.h"
#include <ansi.h>
#include <stdio.h>
#include <string.h>
#include <util/path.h>

/** @brief Candidates one completion can offer. */
#define COMPLETE_MAX 64

static char g_history[HISTORY_MAX][LINE_BUFFER_SIZE]; /**< The history ring. */
static int g_hist_head;                               /**< Next slot to write. */
static int g_hist_count;                              /**< Lines held. */

void HistoryPush(const char *line)
{
    int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (g_hist_count > 0 && strcmp(g_history[last], line) == 0)
        return;
    strncpy(g_history[g_hist_head], line, LINE_BUFFER_SIZE - 1);
    g_history[g_hist_head][LINE_BUFFER_SIZE - 1] = '\0';
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX)
        g_hist_count++;
}

const char *HistoryGet(int offset)
{
    if (offset < 1 || offset > g_hist_count)
        return NULL;
    return g_history[(g_hist_head - offset + HISTORY_MAX) % HISTORY_MAX];
}

int HistoryCount(void) { return g_hist_count; }

/**
 * @brief Builds the prompt from $PS1, or the default.
 *
 * In $PS1, `\w` is the working directory (with $HOME as `~`), `\$` is `$`, `\e` is the escape
 * character and `\\` is a backslash.
 *
 * @param[out] out The prompt.
 * @param[in]  cap Size of @p out.
 */
static void BuildPrompt(char *out, size_t cap)
{
    const char *ps1 = VarGet("PS1");
    if (!ps1 || !ps1[0]) {
        snprintf(out, cap, "%s", ANSI_BOLD ANSI_CYAN "zzsh " ANSI_GREEN "~>" ANSI_RESET);
        return;
    }

    size_t n = 0;
    for (const char *p = ps1; *p && n + 1 < cap; p++) {
        if (*p != '\\' || !p[1]) {
            out[n++] = *p;
            continue;
        }
        p++;
        char tmp[PATH_MAX_LEN];
        const char *add = NULL;
        if (*p == 'w') {
            const char *home = VarGet("HOME");
            size_t hl = home ? strlen(home) : 0;
            if (hl > 1 && strncmp(g_cwd, home, hl) == 0 && (g_cwd[hl] == '/' || !g_cwd[hl])) {
                snprintf(tmp, sizeof(tmp), "~%s", g_cwd + hl);
                add = tmp;
            } else {
                add = g_cwd;
            }
        } else if (*p == '$') {
            add = "$";
        } else if (*p == 'e') {
            add = "\033";
        } else if (*p == '\\') {
            add = "\\";
        } else {
            tmp[0] = '\\';
            tmp[1] = *p;
            tmp[2] = '\0';
            add = tmp;
        }
        size_t al = strlen(add);
        if (n + al + 1 >= cap)
            break;
        memcpy(out + n, add, al);
        n += al;
    }
    out[n] = '\0';
}

/** @brief The line being edited. */
typedef struct {
    char text[LINE_BUFFER_SIZE]; /**< Its characters. */
    size_t len;                  /**< How many. */
    size_t pos;                  /**< Cursor position, 0 to len. */
    size_t cap;                  /**< Room, including the terminator. */
} Edit;

/**
 * @brief Redraws the prompt and the line, and puts the cursor back where it was.
 *
 * @param[in] prompt The prompt.
 * @param[in] e      The line.
 */
static void Redraw(const char *prompt, const Edit *e)
{
    printf("\r%s%.*s\033[K", prompt, (int)e->len, e->text);
    if (e->len > e->pos)
        printf("\033[%dD", (int)(e->len - e->pos));
}

/**
 * @brief Inserts text at the cursor.
 *
 * @param[in,out] e The line.
 * @param[in]     s The text.
 * @param[in]     n Its length.
 * @return false if it does not fit.
 */
static bool Insert(Edit *e, const char *s, size_t n)
{
    if (e->len + n >= e->cap)
        return false;
    memmove(e->text + e->pos + n, e->text + e->pos, e->len - e->pos);
    memcpy(e->text + e->pos, s, n);
    e->len += n;
    e->pos += n;
    e->text[e->len] = '\0';
    return true;
}

/**
 * @brief Deletes @p n characters starting at @p from.
 *
 * @param[in,out] e    The line.
 * @param[in]     from Where to start.
 * @param[in]     n    How many.
 */
static void Delete(Edit *e, size_t from, size_t n)
{
    memmove(e->text + from, e->text + from + n, e->len - from - n);
    e->len -= n;
    e->text[e->len] = '\0';
    if (e->pos > from)
        e->pos = e->pos >= from + n ? e->pos - n : from;
}

/** @brief What a completion round collected. */
typedef struct {
    char name[COMPLETE_MAX][64]; /**< Candidate names; directories end in '/'. */
    int n;                       /**< How many. */
} Matches;

/**
 * @brief Adds a candidate if it starts with @p base and is not there already.
 *
 * @param[in,out] m     The candidates.
 * @param[in]     name  The name to consider.
 * @param[in]     isdir Whether it is a directory.
 * @param[in]     base  The prefix typed so far.
 */
static void AddMatch(Matches *m, const char *name, bool isdir, const char *base)
{
    size_t bl = strlen(base);
    if (m->n >= COMPLETE_MAX || strncmp(name, base, bl) != 0)
        return;
    if (name[0] == '.' && base[0] != '.')
        return;
    char full[64];
    snprintf(full, sizeof(full), "%s%s", name, isdir ? "/" : "");
    for (int i = 0; i < m->n; i++) {
        if (strcmp(m->name[i], full) == 0)
            return;
    }
    strcpy(m->name[m->n++], full);
}

/**
 * @brief Collects the entries of a directory that start with @p base.
 *
 * @param[in,out] m        The candidates.
 * @param[in]     dir      Directory, relative to the working directory or absolute.
 * @param[in]     base     The prefix typed so far.
 * @param[in]     dirs_only Whether to offer only directories.
 */
static void ListDir(Matches *m, const char *dir, const char *base, bool dirs_only)
{
    char abs[PATH_MAX_LEN];
    if (!ShEnsureFsd() || !PathResolve(g_cwd, dir, abs, sizeof(abs)))
        return;
    FsdDirEntry ents[8];
    for (uint32_t start = 0;; start += 8) {
        uint32_t got = 0;
        if (FsdReadDir(ShFsd(), abs, start, ents, 8, &got) != ZUZU_OK || got == 0)
            break;
        for (uint32_t i = 0; i < got; i++) {
            bool isdir = ents[i].type == FSD_TYPE_DIR;
            if (!dirs_only || isdir)
                AddMatch(m, ents[i].name, isdir, base);
        }
        if (got < 8)
            break;
    }
}

/**
 * @brief Completes the word under the cursor.
 *
 * The first word is completed from the builtins and $PATH, later ones from the file system. One
 * match is filled in; several fill in their common prefix, and a second Tab lists them.
 *
 * @param[in,out] e      The line.
 * @param[in]     prompt The prompt, for redrawing.
 * @param[in,out] listed Whether the last key was an ambiguous Tab.
 */
static void Complete(Edit *e, const char *prompt, bool *listed)
{
    size_t ws = e->pos;
    while (ws > 0 && e->text[ws - 1] != ' ')
        ws--;
    char word[LINE_BUFFER_SIZE];
    size_t wl = e->pos - ws;
    memcpy(word, e->text + ws, wl);
    word[wl] = '\0';

    bool command = true;
    for (size_t i = 0; i < ws; i++) {
        if (e->text[i] != ' ')
            command = false;
    }

    static Matches m;
    m.n = 0;
    const char *base = word;
    if (command && !strchr(word, '/')) {
        for (int i = 0; ShBuiltinAt(i); i++)
            AddMatch(&m, ShBuiltinAt(i), false, word);
        const char *path = VarGet("PATH");
        if (!path || !path[0])
            path = "/bin";
        while (*path) {
            const char *end = strchr(path, ':');
            size_t dl = end ? (size_t)(end - path) : strlen(path);
            char dir[PATH_MAX_LEN];
            if (dl > 0 && dl < sizeof(dir)) {
                memcpy(dir, path, dl);
                dir[dl] = '\0';
                ListDir(&m, dir, word, false);
            }
            path = end ? end + 1 : path + dl;
        }
    } else {
        const char *slash = strrchr(word, '/');
        char dir[PATH_MAX_LEN] = ".";
        if (slash) {
            size_t dl = (size_t)(slash - word);
            if (dl == 0) {
                strcpy(dir, "/");
            } else if (dl < sizeof(dir)) {
                memcpy(dir, word, dl);
                dir[dl] = '\0';
            }
            base = slash + 1;
        }
        ListDir(&m, dir, base, false);
    }

    if (m.n == 0)
        return;

    /* the longest prefix every candidate shares */
    size_t common = strlen(m.name[0]);
    for (int i = 1; i < m.n; i++) {
        size_t k = 0;
        while (k < common && m.name[i][k] == m.name[0][k])
            k++;
        common = k;
    }
    size_t have = strlen(base);
    if (common > have) {
        Insert(e, m.name[0] + have, common - have);
        if (m.n == 1 && m.name[0][common - 1] != '/')
            Insert(e, " ", 1);
        Redraw(prompt, e);
        *listed = false;
        return;
    }
    if (m.n > 1 && *listed) {
        printf("\r\n");
        for (int i = 0; i < m.n; i++)
            printf("%s%s", m.name[i], (i + 1) % 4 == 0 ? "\r\n" : "  ");
        printf("\r\n");
        Redraw(prompt, e);
    }
    *listed = m.n > 1;
}

/** @brief Escape-sequence parser state. */
typedef enum {
    ST_NORMAL, /**< Reading plain characters. */
    ST_ESC,    /**< After ESC. */
    ST_CSI,    /**< Inside ESC [ ... */
} EscState;

/**
 * @brief Moves in the history.
 *
 * @param[in,out] e      The line.
 * @param[in,out] pos    How far back in the history we are, 0 for the line being typed.
 * @param[in,out] saved  The line being typed, kept while browsing.
 * @param[in]     older  true for Up, false for Down.
 * @param[in]     prompt The prompt.
 */
static void BrowseHistory(Edit *e, int *pos, char *saved, bool older, const char *prompt)
{
    const char *h = NULL;
    if (older && *pos < g_hist_count) {
        if (*pos == 0)
            memcpy(saved, e->text, e->len + 1);
        h = HistoryGet(++*pos);
    } else if (!older && *pos > 0) {
        h = --*pos ? HistoryGet(*pos) : saved;
    }
    if (!h)
        return;
    e->len = strlen(h);
    memcpy(e->text, h, e->len + 1);
    e->pos = e->len;
    Redraw(prompt, e);
}

bool ReadLine(char *line, size_t size)
{
    Edit e = {.cap = size < LINE_BUFFER_SIZE ? size : LINE_BUFFER_SIZE};
    char prompt[256];
    char saved[LINE_BUFFER_SIZE] = "";
    int hist_pos = 0;
    EscState st = ST_NORMAL;
    int param = 0;
    bool listed = false;

    BuildPrompt(prompt, sizeof(prompt));
    printf("%s", prompt);

    for (;;) {
        int c = getchar();
        if (c == EOF)
            return false;

        if (st == ST_ESC) {
            st = (c == '[' || c == 'O') ? ST_CSI : ST_NORMAL;
            param = 0;
            continue;
        }
        if (st == ST_CSI) {
            if (c >= '0' && c <= '9') {
                param = param * 10 + (c - '0');
                continue;
            }
            st = ST_NORMAL;
            if (c == 'A') {
                BrowseHistory(&e, &hist_pos, saved, true, prompt);
            } else if (c == 'B') {
                BrowseHistory(&e, &hist_pos, saved, false, prompt);
            } else if (c == 'C' && e.pos < e.len) {
                e.pos++;
                printf("\033[C");
            } else if (c == 'D' && e.pos > 0) {
                e.pos--;
                printf("\033[D");
            } else if (c == 'H' || (c == '~' && (param == 1 || param == 7))) {
                e.pos = 0;
                Redraw(prompt, &e);
            } else if (c == 'F' || (c == '~' && (param == 4 || param == 8))) {
                e.pos = e.len;
                Redraw(prompt, &e);
            } else if (c == '~' && param == 3 && e.pos < e.len) {
                Delete(&e, e.pos, 1);
                Redraw(prompt, &e);
            }
            continue;
        }

        bool was_tab = c == '\t';
        if (c == '\033') {
            st = ST_ESC;
        } else if (c == '\r' || c == '\n') {
            printf("\n");
            memcpy(line, e.text, e.len + 1);
            return true;
        } else if (c == 0x04) { /* ^D */
            if (e.len == 0)
                return false;
            if (e.pos < e.len) {
                Delete(&e, e.pos, 1);
                Redraw(prompt, &e);
            }
        } else if (c == 0x03) { /* ^C */
            printf("^C\n");
            line[0] = '\0';
            return true;
        } else if (c == 0x01) { /* ^A */
            e.pos = 0;
            Redraw(prompt, &e);
        } else if (c == 0x05) { /* ^E */
            e.pos = e.len;
            Redraw(prompt, &e);
        } else if (c == 0x02 && e.pos > 0) { /* ^B */
            e.pos--;
            printf("\033[D");
        } else if (c == 0x06 && e.pos < e.len) { /* ^F */
            e.pos++;
            printf("\033[C");
        } else if (c == 0x0B) { /* ^K */
            e.len = e.pos;
            e.text[e.len] = '\0';
            Redraw(prompt, &e);
        } else if (c == 0x15) { /* ^U */
            Delete(&e, 0, e.pos);
            e.pos = 0;
            Redraw(prompt, &e);
        } else if (c == 0x17) { /* ^W */
            size_t from = e.pos;
            while (from > 0 && e.text[from - 1] == ' ')
                from--;
            while (from > 0 && e.text[from - 1] != ' ')
                from--;
            Delete(&e, from, e.pos - from);
            e.pos = from;
            Redraw(prompt, &e);
        } else if (c == 0x0C) { /* ^L */
            printf("%s", ANSI_CLEAR);
            Redraw(prompt, &e);
        } else if (c == '\t') {
            Complete(&e, prompt, &listed);
        } else if (c == 127 || c == '\b') {
            if (e.pos > 0) {
                Delete(&e, e.pos - 1, 1);
                Redraw(prompt, &e);
            }
        } else if (c >= 0x20 && c < 0x7f) {
            char ch = (char)c;
            bool at_end = e.pos == e.len;
            if (Insert(&e, &ch, 1)) {
                if (at_end)
                    printf("%c", c);
                else
                    Redraw(prompt, &e);
            }
        }
        if (!was_tab)
            listed = false;
    }
}
