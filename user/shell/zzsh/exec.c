/**
 * @file exec.c
 *
 * @brief Running commands: builtins, programs, pipelines and redirects.
 */

#include "zzsh.h"
#include <ansi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/imgcache.h>
#include <util/path.h>
#include <util/pipe.h>
#include <util/spawn.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

int g_status;
bool g_exit;
char g_cwd[PATH_MAX_LEN] = "/";

static FsdConn g_fsd;       /**< Connection to the file service. */
static ImgCache g_images;   /**< Program images already read, keyed by path and mtime. */
static int64_t g_out_fd = -1; /**< File a builtin's output goes to, or -1 for the terminal. */

/** @brief Most a builtin may print when it is part of a pipeline. */
#define CAPTURE_MAX 65536

/** @brief Output a builtin printed inside a pipeline, held until the pipes can take it. */
typedef struct {
    char *buf;    /**< The bytes, or NULL if none. */
    size_t len;   /**< How many. */
    size_t cap;   /**< Room allocated. */
} Capture;

static Capture *g_capture; /**< Where ShPrintf() puts output while a pipeline stage runs, or NULL. */

/** @brief Exit status of a command that was not found. */
#define STATUS_NOT_FOUND 127
/** @brief Exit status of a command that could not be started. */
#define STATUS_CANNOT_RUN 126
/** @brief Exit status of a task that faulted. */
#define STATUS_FAULT 139
/** @brief Longest environment handed to a child. */
#define ENV_MAX 4096
/** @brief Longest argument list handed to a child. */
#define ARGS_MAX 2048
/** @brief Longest a script file may be. */
#define SCRIPT_MAX 16384
/** @brief How long a wait with redirects sleeps between looks at the pipes. */
#define PUMP_POLL_MS 5

bool ShEnsureFsd(void)
{
    if (g_fsd.ready)
        return true;
    return FsdConnect(&g_fsd, FSD_SHM_DEFAULT) == ZUZU_OK;
}

FsdConn *ShFsd(void) { return &g_fsd; }

void ShPrintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;

    if (g_capture) {
        Capture *cp = g_capture;
        if (cp->len + (size_t)n > CAPTURE_MAX)
            return;
        if (cp->len + (size_t)n > cp->cap) {
            size_t want = cp->cap ? cp->cap * 2 : 1024;
            while (want < cp->len + (size_t)n)
                want *= 2;
            char *bigger = realloc(cp->buf, want);
            if (!bigger)
                return;
            cp->buf = bigger;
            cp->cap = want;
        }
        memcpy(cp->buf + cp->len, buf, (size_t)n);
        cp->len += (size_t)n;
        return;
    }
    if (g_out_fd < 0) {
        printf("%s", buf);
        return;
    }
    size_t off = 0;
    while (off < (size_t)n) {
        uint32_t put = 0;
        if (FsdWrite(&g_fsd, (uint32_t)g_out_fd, buf + off, (uint32_t)n - (uint32_t)off, &put) !=
                ZUZU_OK ||
            put == 0)
            return;
        off += put;
    }
}

/**
 * @brief Stats a path in the file service.
 *
 * @param[in]  path Absolute path.
 * @param[out] st   The result.
 * @return true if it exists.
 */
static bool StatPath(const char *path, FsdStat *st)
{
    return ShEnsureFsd() && strlen(path) < PATH_MAX_LEN && FsdGetStat(&g_fsd, path, st) == ZUZU_OK;
}

bool ShChdir(const char *path)
{
    char abs[PATH_MAX_LEN];
    FsdStat st;
    if (!PathResolve(g_cwd, path, abs, sizeof(abs)))
        return false;
    if (strcmp(abs, "/") != 0 && (!StatPath(abs, &st) || st.type != FSD_TYPE_DIR))
        return false;

    VarExport("OLDPWD", g_cwd);
    strcpy(g_cwd, abs);
    VarExport("PWD", g_cwd);
    return true;
}

/* ---- program lookup and loading ---- */

bool ShFindProgram(const char *name, char *out, size_t cap)
{
    FsdStat st;
    if (strchr(name, '/')) {
        return PathResolve(g_cwd, name, out, cap) && StatPath(out, &st) &&
               st.type == FSD_TYPE_FILE;
    }

    const char *path = VarGet("PATH");
    if (!path || !path[0])
        path = "/bin";
    while (*path) {
        const char *end = strchr(path, ':');
        size_t dl = end ? (size_t)(end - path) : strlen(path);
        char dir[PATH_MAX_LEN], cand[PATH_MAX_LEN];
        if (dl > 0 && dl < sizeof(dir)) {
            memcpy(dir, path, dl);
            dir[dl] = '\0';
            if (snprintf(cand, sizeof(cand), "%s/%s", dir, name) < (int)sizeof(cand) &&
                PathResolve(g_cwd, cand, out, cap) && StatPath(out, &st) &&
                st.type == FSD_TYPE_FILE)
                return true;
        }
        path = end ? end + 1 : path + dl;
    }
    return false;
}

/**
 * @brief Returns the image object for a program, from the cache when the file is unchanged.
 *
 * @param[in]  path  Absolute path.
 * @param[out] len   Receives the file size.
 * @param[out] owned Set to true when the caller must close the handle, false when the cache
 *                   keeps it.
 * @return The image handle, or -1.
 */
static Handle LoadImage(const char *path, size_t *len, bool *owned)
{
    FsdStat st;
    if (!StatPath(path, &st) || st.type != FSD_TYPE_FILE || st.size == 0)
        return -1;

    *len = st.size;
    Handle obj = ImgCacheGet(&g_images, path, st.size, st.mtime);
    if (obj >= 0) {
        *owned = false;
        return obj;
    }
    if (FsdReadImage(&g_fsd, path, &obj, len) != ZUZU_OK)
        return -1;
    *owned = !ImgCachePut(&g_images, path, st.size, st.mtime, obj);
    return obj;
}

/* ---- builtins ---- */

/** @brief A builtin command: takes the words, returns the exit status. */
typedef int (*BuiltinFn)(int argc, char **argv);

static int BiCd(int argc, char **argv)
{
    const char *dest = argc > 1 ? argv[1] : VarGet("HOME");
    bool back = false;
    if (argc > 2) {
        ShPrintf("cd: too many arguments\n");
        return 1;
    }
    if (dest && strcmp(dest, "-") == 0) {
        dest = VarGet("OLDPWD");
        back = true;
    }
    if (!dest || !dest[0]) {
        ShPrintf("cd: no directory\n");
        return 1;
    }
    char target[VAR_VALUE_MAX];
    snprintf(target, sizeof(target), "%s", dest);
    if (!ShChdir(target)) {
        ShPrintf("cd: %s: not a directory\n", target);
        return 1;
    }
    if (back)
        ShPrintf("%s\n", g_cwd);
    return 0;
}

static int BiPwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ShPrintf("%s\n", g_cwd);
    return 0;
}

static int BiEcho(int argc, char **argv)
{
    int i = 1;
    bool newline = true;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        newline = false;
        i++;
    }
    for (; i < argc; i++)
        ShPrintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    if (newline)
        ShPrintf("\n");
    return 0;
}

static int BiExport(int argc, char **argv)
{
    if (argc == 1) {
        for (int i = 0; i < VarSlots(); i++) {
            const char *n, *v;
            bool ex;
            if (VarAt(i, &n, &v, &ex) && ex)
                ShPrintf("export %s=\"%s\"\n", n, v);
        }
        return 0;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        char name[VAR_NAME_MAX];
        size_t nl = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        if (!VarNameOk(argv[i], nl)) {
            ShPrintf("export: '%s': not a valid name\n", argv[i]);
            rc = 1;
            continue;
        }
        memcpy(name, argv[i], nl);
        name[nl] = '\0';
        if (!VarExport(name, eq ? eq + 1 : NULL)) {
            ShPrintf("export: cannot set %s\n", name);
            rc = 1;
        }
    }
    return rc;
}

static int BiUnset(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        VarUnset(argv[i]);
    return 0;
}

/**
 * @brief Prints variables.
 *
 * @param[in] exported_only Whether to list only exported ones, as `env` does.
 */
static void ListVars(bool exported_only)
{
    for (int i = 0; i < VarSlots(); i++) {
        const char *n, *v;
        bool ex;
        if (VarAt(i, &n, &v, &ex) && (ex || !exported_only))
            ShPrintf("%s=%s\n", n, v);
    }
}

static int BiSet(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ListVars(false);
    return 0;
}

static int BiEnv(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ListVars(true);
    return 0;
}

static int BiExit(int argc, char **argv)
{
    g_exit = true;
    return argc > 1 ? atoi(argv[1]) & 0xFF : g_status;
}

static int BiTrue(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

static int BiFalse(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 1;
}

static int BiClear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ShPrintf("%s", ANSI_CLEAR);
    return 0;
}

static int BiHistory(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = HistoryCount(); i >= 1; i--)
        ShPrintf("%4d  %s\n", HistoryCount() - i + 1, HistoryGet(i));
    return 0;
}

static int BiSleep(int argc, char **argv)
{
    if (argc != 2) {
        ShPrintf("usage: sleep <seconds>\n");
        return 1;
    }
    Sleep((Duration)atoi(argv[1]) * 1000u);
    return 0;
}

static int BiType(int argc, char **argv)
{
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[PATH_MAX_LEN];
        if (ShIsBuiltin(argv[i])) {
            ShPrintf("%s is a shell builtin\n", argv[i]);
        } else if (ShFindProgram(argv[i], path, sizeof(path))) {
            ShPrintf("%s is %s\n", argv[i], path);
        } else {
            ShPrintf("%s: not found\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int BiSource(int argc, char **argv)
{
    if (argc != 2) {
        ShPrintf("usage: source <file>\n");
        return 1;
    }
    char path[PATH_MAX_LEN];
    if (!PathResolve(g_cwd, argv[1], path, sizeof(path))) {
        ShPrintf("source: path too long\n");
        return 1;
    }
    int rc = ShSource(path);
    if (rc < 0) {
        ShPrintf("source: %s: cannot read\n", argv[1]);
        return 1;
    }
    return rc;
}

/**
 * @brief Parses a decimal integer; false if @p s is not one.
 *
 * @param[in]  s   The text.
 * @param[out] out The value.
 * @return true on success.
 */
static bool ParseInt(const char *s, long *out)
{
    char *end;
    *out = strtol(s, &end, 10);
    return s[0] != '\0' && *end == '\0';
}

/**
 * @brief Evaluates the arguments of `test` (without a closing `]`).
 *
 * @param[in] n The number of words.
 * @param[in] a The words.
 * @return 0 if true, 1 if false, 2 if the expression is malformed.
 */
static int TestExpr(int n, char **a)
{
    if (n > 0 && strcmp(a[0], "!") == 0) {
        int r = TestExpr(n - 1, a + 1);
        return r == 2 ? 2 : !r;
    }
    if (n == 0)
        return 1;
    if (n == 1)
        return a[0][0] == '\0';
    if (n == 2) {
        char path[PATH_MAX_LEN];
        FsdStat st;
        if (strcmp(a[0], "-z") == 0)
            return a[1][0] != '\0';
        if (strcmp(a[0], "-n") == 0)
            return a[1][0] == '\0';
        bool have = PathResolve(g_cwd, a[1], path, sizeof(path)) && StatPath(path, &st);
        if (strcmp(a[0], "-e") == 0)
            return !have;
        if (strcmp(a[0], "-f") == 0)
            return !(have && st.type == FSD_TYPE_FILE);
        if (strcmp(a[0], "-d") == 0)
            return !(have && st.type == FSD_TYPE_DIR);
        return 2;
    }
    if (n == 3) {
        if (strcmp(a[1], "=") == 0 || strcmp(a[1], "==") == 0)
            return strcmp(a[0], a[2]) != 0;
        if (strcmp(a[1], "!=") == 0)
            return strcmp(a[0], a[2]) == 0;
        long x, y;
        if (!ParseInt(a[0], &x) || !ParseInt(a[2], &y))
            return 2;
        if (strcmp(a[1], "-eq") == 0)
            return x != y;
        if (strcmp(a[1], "-ne") == 0)
            return x == y;
        if (strcmp(a[1], "-lt") == 0)
            return !(x < y);
        if (strcmp(a[1], "-le") == 0)
            return !(x <= y);
        if (strcmp(a[1], "-gt") == 0)
            return !(x > y);
        if (strcmp(a[1], "-ge") == 0)
            return !(x >= y);
    }
    return 2;
}

static int BiTest(int argc, char **argv)
{
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            ShPrintf("[: missing ]\n");
            return 2;
        }
        argc--;
    }
    int r = TestExpr(argc - 1, argv + 1);
    if (r == 2)
        ShPrintf("%s: bad expression\n", argv[0]);
    return r;
}

static int BiHelp(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ShPrintf(ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER ANSI_RESET "\n"
             "  cd [dir|-]  pwd  echo [-n]  export [N[=V]]  unset N  set  env  history\n"
             "  type N  test|[ expr ]  sleep S  source F  true  false  clear  help  exit [n]\n"
             "  tftp [-s ip] F [dest]   fetch a file over TFTP\n\n"
             "  NAME=value cmd   set a variable (for one command, or for the shell if alone)\n"
             "  $NAME ${NAME} $? ~ 'quote' \"quote\" \\x   expansion and quoting\n"
             "  a | b            pipe     a > f  a >> f  a < f   redirect\n"
             "  a ; b   a && b   a || b   several commands on a line\n"
             "  * ?              wildcards    # comment    PS1  prompt (\\w is the directory)\n"
             "  Tab completes; Up/Down is history; ^A ^E ^K ^U ^W ^L edit.\n"
             "  <program>        run it from $PATH, or give a path\n");
    return 0;
}

/** @brief Server tftp fetches from when no -s is given. */
#define TFTP_DEFAULT_SERVER "192.168.1.6"

static int BiTftp(int argc, char **argv)
{
    const char *server = TFTP_DEFAULT_SERVER;
    int i = 1;
    if (argc > 2 && strcmp(argv[1], "-s") == 0) {
        server = argv[2];
        i = 3;
    }
    if (argc - i < 1 || argc - i > 2) {
        ShPrintf("usage: tftp [-s <ip>] <file> [dest]\n");
        return 1;
    }
    const char *file = argv[i];
    const char *base = strrchr(file, '/');
    const char *dest = argc - i == 2 ? argv[i + 1] : (base ? base + 1 : file);
    char path[PATH_MAX_LEN];
    if (!PathResolve(g_cwd, dest, path, sizeof(path)) || !ShEnsureFsd()) {
        ShPrintf("tftp: %s: cannot open\n", dest);
        return 1;
    }
    if (TftpGet(ShFsd(), server, file, path) != 0)
        return 1;
    ImgCacheDrop(&g_images, path);
    return 0;
}

/** @brief The builtin table. */
static const struct {
    const char *name; /**< Command word. */
    BuiltinFn fn;     /**< Implementation. */
} k_builtins[] = {
    {"cd", BiCd},         {"pwd", BiPwd},       {"echo", BiEcho},       {"export", BiExport},
    {"unset", BiUnset},   {"set", BiSet},       {"env", BiEnv},         {"exit", BiExit},
    {"true", BiTrue},     {":", BiTrue},        {"false", BiFalse},     {"clear", BiClear},
    {"history", BiHistory}, {"sleep", BiSleep}, {"type", BiType},       {"source", BiSource},
    {".", BiSource},      {"test", BiTest},     {"[", BiTest},          {"help", BiHelp},
    {"tftp", BiTftp},
};
/** @brief Number of builtins. */
#define N_BUILTINS ((int)(sizeof(k_builtins) / sizeof(k_builtins[0])))

/**
 * @brief Whether a builtin may be a stage of a pipeline.
 *
 * Builtins that change the shell itself (the directory, variables, the shell's life) would be
 * lost or surprising in a pipeline, so they only run on their own.
 *
 * @param[in] name Command word, already known to be a builtin.
 * @return true if it may be piped.
 */
static bool BuiltinPipeable(const char *name)
{
    static const char *const kOwn[] = {"cd", "exit", "export", "unset", "source", ".", "set"};
    for (size_t i = 0; i < sizeof(kOwn) / sizeof(kOwn[0]); i++) {
        if (strcmp(name, kOwn[i]) == 0)
            return false;
    }
    return true;
}

/**
 * @brief Finds a builtin.
 *
 * @param[in] name Command word.
 * @return Its function, or NULL.
 */
static BuiltinFn FindBuiltin(const char *name)
{
    for (int i = 0; i < N_BUILTINS; i++) {
        if (strcmp(k_builtins[i].name, name) == 0)
            return k_builtins[i].fn;
    }
    return NULL;
}

bool ShIsBuiltin(const char *name) { return FindBuiltin(name) != NULL; }

const char *ShBuiltinAt(int i) { return (i >= 0 && i < N_BUILTINS) ? k_builtins[i].name : NULL; }

/* ---- running programs ---- */

/**
 * @brief Starts one command as a program in its own space.
 *
 * @param[in]  c     The command.
 * @param[in]  in    Pipe end for its stdin, or NULL for the terminal.
 * @param[in]  out   Pipe end for its stdout, or NULL for the terminal.
 * @param[out] task  Its task handle.
 * @param[out] pid   Its space handle.
 * @return 0, or STATUS_NOT_FOUND / STATUS_CANNOT_RUN after printing why.
 */
static int SpawnStage(const ShCmd *c, const PipeEnd *in, const PipeEnd *out, Handle *task,
                      Spid *pid)
{
    char path[PATH_MAX_LEN];
    if (!ShFindProgram(c->argv[0], path, sizeof(path))) {
        ShPrintf("zzsh: %s: command not found\n", c->argv[0]);
        return STATUS_NOT_FOUND;
    }

    char argbuf[ARGS_MAX];
    size_t argpos = 0;
    for (int i = 0; i < c->argc; i++) {
        size_t n = strlen(c->argv[i]) + 1;
        if (argpos + n > sizeof(argbuf)) {
            ShPrintf("zzsh: %s: argument list too long\n", c->argv[0]);
            return STATUS_CANNOT_RUN;
        }
        memcpy(argbuf + argpos, c->argv[i], n);
        argpos += n;
    }

    char envbuf[ENV_MAX];
    size_t envlen = 0;
    uint32_t envc = 0;
    if (!VarBuildEnv(envbuf, sizeof(envbuf), &envlen, &envc, c->assign, c->nassign)) {
        ShPrintf("zzsh: %s: environment too large\n", c->argv[0]);
        return STATUS_CANNOT_RUN;
    }

    size_t len = 0;
    bool owned = true;
    Handle image = LoadImage(path, &len, &owned);
    if (image < 0) {
        ShPrintf("zzsh: %s: cannot read\n", path);
        return STATUS_CANNOT_RUN;
    }

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT,
                      .max_prio = SPAWN_PRIO_INHERIT,
                      .stdin_pipe = in,
                      .stdout_pipe = out,
                      .envbuf = envbuf,
                      .envbuf_len = envlen,
                      .envc = envc};
    Err rc = SpawnProcessObjAttr(image, len, name, argbuf, argpos, (uint32_t)c->argc, &attr, pid,
                                 task);
    if (owned)
        HandleClose(image);
    else if (rc != ZUZU_OK)
        ImgCacheDrop(&g_images, path);
    if (rc != ZUZU_OK) {
        ShPrintf("zzsh: %s: cannot run (%s)\n", c->argv[0], StrToError(rc));
        return STATUS_CANNOT_RUN;
    }
    return 0;
}

/** @brief Moves bytes between the shell's files and the pipes of a pipeline. */
typedef struct {
    bool in_active;     /**< Still feeding the first stage from a file. */
    uint32_t in_fd;     /**< The file being read. */
    PipeEnd *in_pipe;   /**< The shell's writer end of the pipe into stage 0. */
    uint8_t ibuf[512];  /**< Bytes read from the file, not yet in the pipe. */
    size_t ilen;        /**< Bytes in @c ibuf. */
    size_t ioff;        /**< Bytes of @c ibuf already sent. */
    bool ieof;          /**< The file is used up. */
    bool out_active;    /**< Still collecting the last stage's output into a file. */
    uint32_t out_fd;    /**< The file being written. */
    PipeEnd *out_pipe;  /**< The shell's reader end of the pipe from the last stage. */
    struct {
        const Capture *cap; /**< What a builtin stage printed, or NULL if the stage is a program. */
        size_t off;         /**< Bytes already sent. */
        PipeEnd *w;         /**< The shell's writer end of the pipe the stage's output goes into. */
        bool active;        /**< Still sending. */
    } feed[SH_MAX_STAGES];
    struct {
        PipeEnd *r;  /**< The shell's reader end of the pipe into a builtin stage. */
        bool active; /**< Still discarding what arrives. */
    } drain[SH_MAX_STAGES];
} Pump;

/**
 * @brief Does one round of the file-to-pipe and pipe-to-file transfers without waiting.
 *
 * @param[in,out] pu The pump.
 * @return true if it moved anything or finished a side.
 */
static bool PumpStep(Pump *pu)
{
    bool did = false;

    if (pu->in_active) {
        if (pu->ioff == pu->ilen && !pu->ieof) {
            uint32_t got = 0;
            if (FsdRead(&g_fsd, pu->in_fd, pu->ibuf, sizeof(pu->ibuf), &got) != ZUZU_OK || got == 0) {
                pu->ieof = true;
            } else {
                pu->ilen = got;
                pu->ioff = 0;
            }
        }
        if (pu->ioff < pu->ilen) {
            int n = PipeWrite(pu->in_pipe, pu->ibuf + pu->ioff, pu->ilen - pu->ioff, TIMEOUT_POLL);
            if (n > 0) {
                pu->ioff += (size_t)n;
                did = true;
            } else if (n == ERR_DEAD) {
                pu->ieof = true; /* the reader is gone: stop feeding it */
                pu->ioff = pu->ilen;
            }
        }
        if (pu->ieof && pu->ioff == pu->ilen) {
            PipeMarkClosed(pu->in_pipe, PIPE_WRITER);
            pu->in_active = false;
            did = true;
        }
    }

    for (int i = 0; i < SH_MAX_STAGES; i++) {
        if (pu->feed[i].active) {
            const Capture *cp = pu->feed[i].cap;
            if (pu->feed[i].off < cp->len) {
                int n = PipeWrite(pu->feed[i].w, cp->buf + pu->feed[i].off,
                                  cp->len - pu->feed[i].off, TIMEOUT_POLL);
                if (n > 0) {
                    pu->feed[i].off += (size_t)n;
                    did = true;
                } else if (n == ERR_DEAD) {
                    pu->feed[i].off = cp->len;
                }
            }
            if (pu->feed[i].off >= cp->len) {
                PipeMarkClosed(pu->feed[i].w, PIPE_WRITER);
                pu->feed[i].active = false;
                did = true;
            }
        }
        if (pu->drain[i].active) {
            uint8_t sink[256];
            int n = PipeRead(pu->drain[i].r, sink, sizeof(sink), TIMEOUT_POLL);
            if (n > 0) {
                did = true;
            } else if (n == 0) {
                pu->drain[i].active = false;
                did = true;
            }
        }
    }

    if (pu->out_active) {
        uint8_t buf[512];
        int n = PipeRead(pu->out_pipe, buf, sizeof(buf), TIMEOUT_POLL);
        if (n > 0) {
            size_t off = 0;
            while (off < (size_t)n) {
                uint32_t put = 0;
                if (FsdWrite(&g_fsd, pu->out_fd, buf + off, (uint32_t)n - (uint32_t)off, &put) !=
                        ZUZU_OK ||
                    put == 0)
                    break;
                off += put;
            }
            did = true;
        } else if (n == 0) {
            pu->out_active = false; /* the last stage closed its end and the pipe is drained */
            did = true;
        }
    }
    return did;
}

/**
 * @brief Whether a builtin stage's output is still being sent or its input discarded.
 *
 * @param[in] pu The pump.
 * @return true if either is going on.
 */
static bool PumpBusy(const Pump *pu)
{
    for (int i = 0; i < SH_MAX_STAGES; i++) {
        if (pu->feed[i].active || pu->drain[i].active)
            return true;
    }
    return false;
}

/**
 * @brief Opens a redirect target.
 *
 * @param[in]  file   The path as written.
 * @param[in]  mode   FSD_MODE_* flags.
 * @param[out] fd     The open file.
 * @return true on success, after printing why on failure.
 */
static bool OpenRedirect(const char *file, uint32_t mode, uint32_t *fd)
{
    char path[PATH_MAX_LEN];
    if (!PathResolve(g_cwd, file, path, sizeof(path)) || !ShEnsureFsd() ||
        FsdOpen(&g_fsd, path, mode, fd) != ZUZU_OK) {
        ShPrintf("zzsh: %s: cannot open\n", file);
        return false;
    }
    return true;
}

/**
 * @brief Runs an assignment-only or builtin command inside the shell.
 *
 * @param[in] c The command.
 * @return Its exit status.
 */
static int RunInShell(const ShCmd *c)
{
    if (c->argc == 0) {
        for (int i = 0; i < c->nassign; i++) {
            const char *eq = strchr(c->assign[i], '=');
            char name[VAR_NAME_MAX];
            memcpy(name, c->assign[i], (size_t)(eq - c->assign[i]));
            name[eq - c->assign[i]] = '\0';
            if (!VarSet(name, eq + 1)) {
                ShPrintf("zzsh: cannot set %s\n", name);
                return 1;
            }
        }
        return 0;
    }

    if (c->in) {
        ShPrintf("zzsh: %s: input redirection is not supported on builtins\n", c->argv[0]);
        return 1;
    }
    uint32_t fd = 0;
    if (c->out) {
        uint32_t mode = FSD_MODE_WRITE | (c->append ? FSD_MODE_OPEN_APPEND : FSD_MODE_CREATE_ALWAYS);
        if (!OpenRedirect(c->out, mode, &fd))
            return 1;
        g_out_fd = fd;
    }
    int rc = FindBuiltin(c->argv[0])(c->argc, (char **)c->argv);
    if (c->out) {
        g_out_fd = -1;
        FsdClose(&g_fsd, fd);
    }
    return rc;
}

/**
 * @brief Turns the end of a stage into a shell exit status.
 *
 * @param[in] task The stage's task.
 * @return Its exit status; 139 if it faulted.
 */
static int StageStatus(Handle task)
{
    TaskWaitResult tw = WaitOnTask(task, TIMEOUT_POLL);
    if (tw.status != ZUZU_OK)
        return STATUS_CANNOT_RUN;
    return tw.outcome == TASK_FAULTED ? STATUS_FAULT : (tw.value & 0xFF);
}

/**
 * @brief Runs a pipeline of programs, with redirects on its ends.
 *
 * The shell keeps both ends of every pipe so it can tell a stage's neighbours when that stage is
 * gone, whether it exited, faulted or was killed.
 *
 * @param[in] pl The pipeline.
 * @return The status of its last stage.
 */
static int RunPrograms(const ShPipeline *pl)
{
    int n = pl->n;
    const ShCmd *first = &pl->cmd[0];
    const ShCmd *last = &pl->cmd[n - 1];
    for (int i = 0; i < n; i++) {
        if ((pl->cmd[i].in && i > 0) || (pl->cmd[i].out && i < n - 1)) {
            ShPrintf("zzsh: a redirect belongs on the first or last command of a pipeline\n");
            return 2;
        }
        if (FindBuiltin(pl->cmd[i].argv[0]) && pl->cmd[i].in) {
            ShPrintf("zzsh: %s: input redirection is not supported on builtins\n",
                     pl->cmd[i].argv[0]);
            return 1;
        }
    }
    if (!ShEnsureFsd()) {
        ShPrintf("zzsh: fsd unavailable\n");
        return STATUS_CANNOT_RUN;
    }

    Pump pu = {0};
    uint32_t in_fd = 0, out_fd = 0;
    bool in_open = false, out_open = false;
    if (first->in) {
        if (!OpenRedirect(first->in, FSD_MODE_READ, &in_fd))
            return 1;
        in_open = true;
    }
    if (last->out) {
        uint32_t mode = FSD_MODE_WRITE | (last->append ? FSD_MODE_OPEN_APPEND : FSD_MODE_CREATE_ALWAYS);
        if (!OpenRedirect(last->out, mode, &out_fd)) {
            if (in_open)
                FsdClose(&g_fsd, in_fd);
            return 1;
        }
        out_open = true;
    }

    /* pipe k joins stage k to stage k+1; the shell's own pipes follow them */
    PipeEnd pw[SH_MAX_STAGES + 1], pr[SH_MAX_STAGES + 1];
    int pipes = 0;
    int in_pipe = -1, out_pipe = -1;
    for (int i = 0; i < n - 1; i++) {
        if (CreatePipe(PIPE_SIZE_DEFAULT, &pw[pipes], &pr[pipes]) != ZUZU_OK)
            goto fail_pipes;
        pipes++;
    }
    if (in_open) {
        in_pipe = pipes;
        if (CreatePipe(PIPE_SIZE_DEFAULT, &pw[pipes], &pr[pipes]) != ZUZU_OK)
            goto fail_pipes;
        pipes++;
    }
    if (out_open) {
        out_pipe = pipes;
        if (CreatePipe(PIPE_SIZE_DEFAULT, &pw[pipes], &pr[pipes]) != ZUZU_OK)
            goto fail_pipes;
        pipes++;
    }

    Handle task[SH_MAX_STAGES];
    Spid pid[SH_MAX_STAGES];
    int status[SH_MAX_STAGES] = {0};
    Capture cap[SH_MAX_STAGES] = {0};
    bool is_builtin[SH_MAX_STAGES] = {false};
    for (int i = 0; i < n; i++) {
        task[i] = -1;
        BuiltinFn fn = FindBuiltin(pl->cmd[i].argv[0]);
        if (!fn)
            continue;
        is_builtin[i] = true;
        g_capture = &cap[i];
        status[i] = fn(pl->cmd[i].argc, (char **)pl->cmd[i].argv);
        g_capture = NULL;
    }

    stdio_close_tty();

    int started = 0;
    int spawn_status = 0;
    for (; started < n; started++) {
        if (is_builtin[started])
            continue;
        const PipeEnd *in = started > 0 ? &pr[started - 1] : (in_open ? &pr[in_pipe] : NULL);
        const PipeEnd *out = started < n - 1 ? &pw[started] : (out_open ? &pw[out_pipe] : NULL);
        spawn_status = SpawnStage(&pl->cmd[started], in, out, &task[started], &pid[started]);
        if (spawn_status != 0)
            break;
    }
    if (started < n) {
        /* A stage failed to start: the rest would wait on it forever. */
        for (int i = 0; i < started; i++) {
            if (is_builtin[i])
                continue;
            HandleDestroy((Handle)pid[i]);
            HandleClose(task[i]);
        }
        started = 0;
        status[0] = spawn_status;
        goto cleanup;
    }

    if (in_open) {
        pu.in_active = true;
        pu.in_fd = in_fd;
        pu.in_pipe = &pw[in_pipe];
    }
    if (out_open) {
        pu.out_active = true;
        pu.out_fd = out_fd;
        pu.out_pipe = &pr[out_pipe];
    }

    for (int i = 0; i < n; i++) {
        if (!is_builtin[i])
            continue;
        PipeEnd *sink = i < n - 1 ? &pw[i] : (out_open ? &pw[out_pipe] : NULL);
        if (sink) {
            pu.feed[i].cap = &cap[i];
            pu.feed[i].w = sink;
            pu.feed[i].active = true;
        }
        if (i > 0) {
            pu.drain[i].r = &pr[i - 1];
            pu.drain[i].active = true;
        }
    }

    Handle ev = CreateEvent();
    unsigned live = 0;
    for (int i = 0; i < started; i++) {
        if (is_builtin[i])
            continue;
        if (ev < 0 || Bind(EVENT_TASK, ev, task[i], (uint32_t)i) != ZUZU_OK) {
            HandleDestroy((Handle)pid[i]); /* cannot be watched: end it rather than hang */
            status[i] = STATUS_CANNOT_RUN;
            continue;
        }
        live |= 1u << i;
    }

    while (live || pu.in_active || pu.out_active || PumpBusy(&pu)) {
        bool did = PumpStep(&pu);
        bool pumping = pu.in_active || pu.out_active || PumpBusy(&pu);
        if (!live && !pumping)
            break;
        Duration wait = !pumping ? TIMEOUT_INFINITE : (did ? TIMEOUT_POLL : PUMP_POLL_MS);
        if (!live && pumping && !did)
            wait = PUMP_POLL_MS;
        EventWaitResult r = WaitOnEvent(ev, wait, 0, false);
        if (r.status != ZUZU_OK) {
            if (r.status == ERR_TIMEOUT)
                continue;
            break;
        }
        for (int i = 0; i < started; i++) {
            if (!(live & (1u << i)) || !(r.bits & (1u << i)))
                continue;
            live &= ~(1u << i);
            status[i] = StageStatus(task[i]);
            /* stage i's output pipe loses its writer, and its input pipe its reader */
            if (i < n - 1)
                PipeMarkClosed(&pw[i], PIPE_WRITER);
            else if (out_open)
                PipeMarkClosed(&pw[out_pipe], PIPE_WRITER);
            if (i > 0)
                PipeMarkClosed(&pr[i - 1], PIPE_READER);
            else if (in_open)
                PipeMarkClosed(&pr[in_pipe], PIPE_READER);
        }
    }
    for (int i = 0; i < started; i++) {
        if (is_builtin[i])
            continue;
        HandleClose(task[i]);
        HandleDestroy((Handle)pid[i]);
    }
    if (ev >= 0)
        HandleClose(ev);
    if (is_builtin[n - 1] && !out_open && cap[n - 1].len > 0)
        printf("%.*s", (int)cap[n - 1].len, cap[n - 1].buf);

cleanup:
    for (int i = 0; i < n; i++)
        free(cap[i].buf);
    for (int i = 0; i < pipes; i++) {
        ClosePipe(&pw[i]);
        ClosePipe(&pr[i]);
    }
    if (in_open)
        FsdClose(&g_fsd, in_fd);
    if (out_open)
        FsdClose(&g_fsd, out_fd);
    return status[started > 0 ? started - 1 : 0];

fail_pipes:
    ShPrintf("zzsh: cannot create a pipe\n");
    for (int i = 0; i < pipes; i++) {
        ClosePipe(&pw[i]);
        ClosePipe(&pr[i]);
    }
    if (in_open)
        FsdClose(&g_fsd, in_fd);
    if (out_open)
        FsdClose(&g_fsd, out_fd);
    return STATUS_CANNOT_RUN;
}

/**
 * @brief Runs one parsed pipeline.
 *
 * @param[in] pl The pipeline.
 * @return Its exit status.
 */
static int RunPipeline(const ShPipeline *pl)
{
    if (pl->n == 1 && (pl->cmd[0].argc == 0 || FindBuiltin(pl->cmd[0].argv[0])))
        return RunInShell(&pl->cmd[0]);

    for (int i = 0; i < pl->n; i++) {
        if (pl->cmd[i].argc == 0) {
            ShPrintf("zzsh: missing command\n");
            return 2;
        }
        if (FindBuiltin(pl->cmd[i].argv[0]) && !BuiltinPipeable(pl->cmd[i].argv[0])) {
            ShPrintf("zzsh: %s: cannot be used in a pipeline\n", pl->cmd[i].argv[0]);
            return 2;
        }
    }
    return RunPrograms(pl);
}

/* ---- lines and scripts ---- */

int ShRunLine(const char *line)
{
    const char *cursor = line;
    ShSep prev = SEP_NONE;

    for (;;) {
        ShPipeline pl;
        ShSep sep;
        ShParse r = ParsePipeline(&cursor, &pl, &sep);
        if (r == PARSE_ERROR) {
            g_status = 2;
            return g_status;
        }
        bool run = r == PARSE_OK && (prev == SEP_NONE || prev == SEP_SEMI ||
                                     (prev == SEP_AND && g_status == 0) ||
                                     (prev == SEP_OR && g_status != 0));
        if (run)
            g_status = RunPipeline(&pl);
        if (g_exit || sep == SEP_NONE)
            break;
        prev = sep;
    }
    return g_status;
}

int ShSource(const char *path)
{
    FsdStat st;
    uint32_t fd = 0;
    if (!StatPath(path, &st) || st.type != FSD_TYPE_FILE || st.size > SCRIPT_MAX ||
        FsdOpen(&g_fsd, path, FSD_MODE_READ, &fd) != ZUZU_OK)
        return -1;

    char *text = malloc(st.size + 1);
    if (!text) {
        FsdClose(&g_fsd, fd);
        return -1;
    }
    size_t len = 0;
    while (len < st.size) {
        uint32_t got = 0;
        if (FsdRead(&g_fsd, fd, text + len, (uint32_t)(st.size - len), &got) != ZUZU_OK || got == 0)
            break;
        len += got;
    }
    FsdClose(&g_fsd, fd);
    text[len] = '\0';

    int rc = 0;
    for (char *line = text; *line && !g_exit;) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r')
            line[ll - 1] = '\0';
        if (strlen(line) < LINE_BUFFER_SIZE)
            rc = ShRunLine(line);
        line = eol ? eol + 1 : line + ll;
    }
    free(text);
    return rc;
}

void ShInit(void)
{
    VarExport("PATH", "/bin");
    VarExport("HOME", "/");
    VarExport("PWD", g_cwd);
    VarExport("SHELL", "zzsh");
    VarSet("ZZSH_VERSION", ZZSH_VER);
}
