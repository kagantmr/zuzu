/**
 * @file zzsh.h
 *
 * @brief Shared declarations of the zzsh shell: variables, parsing, execution and line editing.
 */

#ifndef ZZSH_H
#define ZZSH_H

#include <fs/fsd_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Version string shown by the banner and `help`. */
#define ZZSH_VER "v2.1"

/** @brief Longest input line, including the terminator. */
#define LINE_BUFFER_SIZE 512
/** @brief Lines kept in the history ring. */
#define HISTORY_MAX 32

/** @brief Longest variable name, including the terminator. */
#define VAR_NAME_MAX 32
/** @brief Longest variable value, including the terminator. */
#define VAR_VALUE_MAX 256
/** @brief Variables the shell can hold. */
#define VAR_MAX 64

/** @brief Words in one command. */
#define SH_MAX_ARGS 32
/** @brief Commands joined by pipes in one pipeline. */
#define SH_MAX_STAGES 8
/** @brief Assignments allowed in front of one command. */
#define SH_MAX_ASSIGNS 8

/** @brief One command of a pipeline, already expanded. */
typedef struct {
    char *argv[SH_MAX_ARGS + 1];    /**< The words; argv[argc] is NULL. */
    int argc;                       /**< Number of words. */
    char *assign[SH_MAX_ASSIGNS];   /**< Leading "NAME=value" words. */
    int nassign;                    /**< Number of assignments. */
    char *in;                       /**< File for `<`, or NULL. */
    char *out;                      /**< File for `>` or `>>`, or NULL. */
    bool append;                    /**< Whether @c out is opened for appending. */
} ShCmd;

/** @brief Commands joined by `|`. */
typedef struct {
    ShCmd cmd[SH_MAX_STAGES]; /**< The stages, first to last. */
    int n;                    /**< Number of stages. */
} ShPipeline;

/** @brief What ended a pipeline in the input. */
typedef enum {
    SEP_NONE, /**< End of the line. */
    SEP_SEMI, /**< `;`: run the next one regardless. */
    SEP_AND,  /**< `&&`: run the next one only after success. */
    SEP_OR,   /**< `||`: run the next one only after failure. */
} ShSep;

/** @brief Result of parsing one pipeline. */
typedef enum {
    PARSE_OK,    /**< A pipeline was read. */
    PARSE_EMPTY, /**< Nothing but blanks or a comment. */
    PARSE_ERROR, /**< A syntax error was reported. */
} ShParse;

/* ---- vars.c ---- */

/** @brief Whether @p name is a valid variable name of @p len characters. */
bool VarNameOk(const char *name, size_t len);
/** @brief The value of a variable, or NULL if it is not set. */
const char *VarGet(const char *name);
/** @brief Sets a variable, keeping its export flag; false if the table or value is full. */
bool VarSet(const char *name, const char *value);
/** @brief Marks a variable for export, optionally giving it a value; false if it cannot exist. */
bool VarExport(const char *name, const char *value);
/** @brief Removes a variable. */
void VarUnset(const char *name);
/** @brief Number of variable slots; entries may be unused. */
int VarSlots(void);
/** @brief Reads slot @p i; false if it is unused. */
bool VarAt(int i, const char **name, const char **value, bool *exported);
/**
 * @brief Builds the environment for a child: every exported variable, then @p extra.
 *
 * @param[out] buf    "NAME=value" strings, NUL-separated.
 * @param[in]  cap    Size of @p buf.
 * @param[out] len    Bytes used.
 * @param[out] count  Number of strings.
 * @param[in]  extra  Per-command assignments that override, or NULL.
 * @param[in]  nextra Number of @p extra.
 * @return false if it does not fit.
 */
bool VarBuildEnv(char *buf, size_t cap, size_t *len, uint32_t *count, char *const *extra,
                 int nextra);

/* ---- parse.c ---- */

/**
 * @brief Reads one pipeline from @p *cursor, expanding variables, quotes, `~` and wildcards.
 *
 * Expansion happens as the pipeline is read, so it sees what earlier pipelines on the line did.
 *
 * @param[in,out] cursor Where to read; advanced past the pipeline and its separator.
 * @param[out]    pl     The pipeline; its strings live until the next call.
 * @param[out]    sep    What ended it.
 * @return PARSE_OK, PARSE_EMPTY or PARSE_ERROR (after printing why).
 */
ShParse ParsePipeline(const char **cursor, ShPipeline *pl, ShSep *sep);

/* ---- exec.c ---- */

/** @brief Exit status of the last command, `$?`. */
extern int g_status;
/** @brief Set by `exit`: the shell should leave. */
extern bool g_exit;
/** @brief Working directory, absolute. */
extern char g_cwd[];
/** @brief Whether the shell reads commands from a terminal, so ^C ends what it runs. */
extern bool g_interactive;

/** @brief Connects to the file service if needed; false if it is not there. */
bool ShEnsureFsd(void);
/** @brief The shared file service connection. */
FsdConn *ShFsd(void);
/** @brief Sets the working directory and PWD/OLDPWD; false if @p path is not a directory. */
bool ShChdir(const char *path);
/** @brief Runs one line: pipelines joined by `;`, `&&` and `||`. Returns the last status. */
int ShRunLine(const char *line);
/** @brief Runs a script file line by line. Returns the last status, or -1 if unreadable. */
int ShSource(const char *path);
/** @brief Sets up the variables and the working directory. */
void ShInit(void);
/** @brief Whether @p name is a builtin. */
bool ShIsBuiltin(const char *name);
/** @brief The name of builtin @p i, or NULL past the last one. */
const char *ShBuiltinAt(int i);
/** @brief Looks @p name up in PATH; false if it is not there. */
bool ShFindProgram(const char *name, char *out, size_t cap);
/** @brief Prints to the shell's output, or to the file a builtin is redirected to. */
void ShPrintf(const char *fmt, ...);

/* ---- edit.c ---- */

/**
 * @brief Reads a line with editing, history and completion.
 *
 * @param[out] line Receives the line.
 * @param[in]  size Size of @p line.
 * @return false at end of input (^D on an empty line).
 */
bool ReadLine(char *line, size_t size);
/** @brief Adds a line to the history. */
void HistoryPush(const char *line);
/** @brief The @p offset-th most recent history line (1 is the latest), or NULL. */
const char *HistoryGet(int offset);
/** @brief Number of lines in the history. */
int HistoryCount(void);

/** @brief Fetches @p file from the TFTP server at @p server into the absolute path @p dest. */
int TftpGet(FsdConn *fsd, const char *server, const char *file, const char *dest);

#endif /* ZZSH_H */
