#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/zuzu.h>
#include <zuzu/syspage.h>
#include <fs/fsd_client.h>

#define MAX_FD 32

int ConsoleWrite(const char *buf, int len);
int ConsoleRead(char *buf, int len);
void ConsoleClose(void);
extern void *sbrk(intptr_t incr);

/* posix fd -> fsd fd; slot 0-2 belong to the console. */
static int fsd_fd[MAX_FD] = { [0 ... MAX_FD - 1] = -1 };
static FsdConn fsd_conn;

/* POSIX open flags -> FSD_MODE_* bits.
 * O_RDONLY is 0, so the access mode must be masked, not bit-tested. */
static uint32_t flags_to_fsd_mode(int flags)
{
    uint32_t mode = 0;

    switch (flags & O_ACCMODE) {
    case O_RDONLY: mode |= FSD_MODE_READ;                     break;
    case O_WRONLY: mode |= FSD_MODE_WRITE;                    break;
    case O_RDWR:   mode |= FSD_MODE_READ | FSD_MODE_WRITE;    break;
    default:       mode |= FSD_MODE_READ;                     break;
    }

    if (flags & O_CREAT) {
        if (flags & O_EXCL)       mode |= FSD_MODE_CREATE_NEW;    /* fail if exists   */
        else if (flags & O_TRUNC) mode |= FSD_MODE_CREATE_ALWAYS; /* create/truncate  */
        else                      mode |= FSD_MODE_OPEN_ALWAYS;   /* create if absent */
    } else if (flags & O_TRUNC) {
        mode |= FSD_MODE_CREATE_ALWAYS;  /* truncate an existing file */
    }

    if (flags & O_APPEND) mode |= FSD_MODE_OPEN_APPEND;

    return mode;
}

/* zuzu Err -> POSIX errno. */
static int err_to_errno(Err e)
{
    switch (e) {
    case ZUZU_OK:        return 0;
    case ERR_NOPERM:     return EACCES;
    case ERR_NOENT:      return ENOENT;
    case ERR_BUSY:       return EBUSY;
    case ERR_NOMEM:      return ENOMEM;
    case ERR_BADARG:     return EINVAL;
    case ERR_BADTYPE:    return EINVAL;
    case ERR_NOSYS:      return ENOSYS;
    case ERR_BADPTR:     return EFAULT;
    case ERR_DEAD:       return EPIPE;
    case ERR_TIMEOUT:    return ETIMEDOUT;
    case ERR_OVERFLOW:   return EOVERFLOW;
    case ERR_BADHANDLE:  return EBADF;
    case ERR_BUFFULL:    return EMFILE;
    case ERR_BUFEMPTY:   return EAGAIN;
    case ERR_SYSDOWN:    return ENODEV;
    case ERR_NOTCONN:    return ENOTCONN;
    case ERR_DUPLICATE:  return EEXIST;
    case ERR_MALFORMED:  return EINVAL;
    case ERR_IO:         return EIO;
    default:             return EIO;
    }
}

static int FsdEnsure(void)
{
    if (fsd_conn.ready)
        return 0;
    Err rc = FsdConnect(&fsd_conn, FSD_SHM_DEFAULT);
    if (rc != ZUZU_OK)
    {
        errno = ENODEV;
        return -1;
    }
    return 0;
}

static int FileSlot(int file)
{
    if (file < 3 || file >= MAX_FD || fsd_fd[file] < 0 || !fsd_conn.ready)
    {
        errno = EBADF;
        return -1;
    }
    return fsd_fd[file];
}

static void FillStat(struct stat *st, const FsdStat *fst)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = (fst->type == FSD_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = (off_t)fst->size;
    st->st_blksize = 512;
}

void *_sbrk(intptr_t incr)
{
    void *p = sbrk(incr);
    if (p == (void *)-1)
        errno = ENOMEM;
    return p;
}

int _isatty(int file)
{
    return (file >= 0 && file <= 2) ? 1 : 0;
}

int _write(int file, char *ptr, int len)
{
    if (len == 0)
        return 0;
    if (len < 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (file == 1 || file == 2)
        return ConsoleWrite(ptr, len);

    int fd = FileSlot(file);
    if (fd < 0)
        return -1;

    int done = 0;
    while (done < len)
    {
        uint32_t put = 0;
        Err rc = FsdWrite(&fsd_conn, (uint32_t)fd, ptr + done, (uint32_t)(len - done), &put);
        if (rc != ZUZU_OK)
        {
            if (done)
                break;
            errno = err_to_errno(rc);
            return -1;
        }
        if (put == 0)
            break;
        done += (int)put;
    }
    return done;
}

void __attribute__((noreturn)) _exit(int status)
{
    ConsoleClose();
    Quit(status);
    for (;;)
        ;
}

int _read(int file, char *ptr, int len)
{
    if (len <= 0)
        return len == 0 ? 0 : (errno = EINVAL, -1);

    if (file == 0)
        return ConsoleRead(ptr, len);

    int fd = FileSlot(file);
    if (fd < 0)
        return -1;

    int done = 0;
    while (done < len)
    {
        uint32_t got = 0;
        Err rc = FsdRead(&fsd_conn, (uint32_t)fd, ptr + done, (uint32_t)(len - done), &got);
        if (rc != ZUZU_OK)
        {
            if (done)
                break;
            errno = err_to_errno(rc);
            return -1;
        }
        if (got == 0)
            break;
        done += (int)got;
    }
    return done;
}

int _close(int file)
{
    if (file >= 0 && file <= 2)
        return 0;
    int fd = FileSlot(file);
    if (fd < 0)
        return -1;

    Err rc = FsdClose(&fsd_conn, (uint32_t)fd);
    fsd_fd[file] = -1;
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    return 0;
}

int _lseek(int file, int ptr, int dir)
{
    if (file >= 0 && file <= 2)
    {
        errno = ESPIPE;
        return -1;
    }
    int fd = FileSlot(file);
    if (fd < 0)
        return -1;

    int64_t pos = 0;
    Err rc = FsdSeek(&fsd_conn, (uint32_t)fd, (int64_t)ptr, (uint32_t)dir, &pos);
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    return (int)pos;
}

int _getpid(void)
{
    return 1;
}

unsigned sleep(unsigned seconds)
{
    Sleep(seconds * 1000u);
    return 0;
}

ssize_t __getline(char **lineptr, size_t *n, FILE *stream);

/* weak: kilo (and possibly other vendored programs) bundle their own
 * getline() fallback and must win over this one. */
__attribute__((weak)) ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return __getline(lineptr, n, stream);
}

int _kill(int pid, int sig)
{
    if (pid == _getpid())
        Quit(sig);
    errno = EINVAL;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    if (file >= 0 && file <= 2)
    {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR;
        return 0;
    }

    int fd = FileSlot(file);
    if (fd < 0)
        return -1;

    FsdStat fst;
    Err rc = FsdFstat(&fsd_conn, (uint32_t)fd, &fst);
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    FillStat(st, &fst);
    return 0;
}

int _open(const char *name, int flags, ...)
{
    if (FsdEnsure() < 0)
        return -1;

    int pfd = 3;
    while (pfd < MAX_FD && fsd_fd[pfd] >= 0)
        pfd++;
    if (pfd == MAX_FD)
    {
        errno = EMFILE;
        return -1;
    }

    uint32_t fd = 0;
    Err rc = FsdOpen(&fsd_conn, name, flags_to_fsd_mode(flags), &fd);
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    fsd_fd[pfd] = (int)fd;
    return pfd;
}

/* No spawn hands us an environment yet; newlib's getenv() still needs the
 * symbol to exist and terminate cleanly. */
static char *__env[1] = { NULL };
char **environ = __env;

/* Wall clock derived from the syspage tick source: boot epoch plus uptime.
 * If the kernel never learned the wall time boot_time_s is 0 and this reads
 * as seconds since boot, which is still monotonic. */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) { errno = EFAULT; return -1; }

    const Syspage *sp = (const Syspage *)SYSPAGE_VA;
    uint32_t hz    = sp->tick_hz ? sp->tick_hz : 1000u;
    uint64_t ticks = sp->uptime_ticks;

    tv->tv_sec  = (time_t)(sp->boot_time_s + ticks / hz);
    tv->tv_usec = (suseconds_t)((ticks % hz) * 1000000ull / hz);
    return 0;
}

/* Elapsed real time in scheduler ticks; no user/kernel split is tracked. */
clock_t _times(struct tms *buf)
{
    const Syspage *sp = (const Syspage *)SYSPAGE_VA;
    clock_t ticks = (clock_t)sp->uptime_ticks;

    if (buf) {
        buf->tms_utime  = ticks;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return ticks;
}

int lstat(const char *name, struct stat *st)
{
    return stat(name, st);
}

int _stat(const char *name, struct stat *st)
{
    if (!name || !st)
    {
        errno = EFAULT;
        return -1;
    }
    if (FsdEnsure() < 0)
        return -1;

    FsdStat fst;
    Err rc = FsdGetStat(&fsd_conn, name, &fst);
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    FillStat(st, &fst);
    return 0;
}

int _unlink(const char *name)
{
    if (!name)
    {
        errno = EFAULT;
        return -1;
    }
    if (FsdEnsure() < 0)
        return -1;

    Err rc = FsdUnlink(&fsd_conn, name);
    if (rc != ZUZU_OK)
    {
        errno = err_to_errno(rc);
        return -1;
    }
    return 0;
}

/* fsd exposes rename but no hard-link command. */
int _link(const char *existing, const char *newpath)
{
    (void)existing;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}
