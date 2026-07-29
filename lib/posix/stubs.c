#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <fcntl.h>
#include <zuzu/zuzu.h>
#include <zuzu/lmsg.h>
#include <zuzu/uspin.h>
#include <zuzu/syspage.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/fsd_protocol.h>
#include <string.h>

#define MAX_FD 32

static Handle console_tty = -1;
static int console_raw_mode = 0;
extern void *sbrk(intptr_t incr);
static int fsd_fd[MAX_FD] = { [0 ... MAX_FD - 1] = -1 };
static Handle fsd_handle = -1;

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

static Handle console_port(void) {
    if (console_tty < 0) {
        Message lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("tty0"), 0);
        if ((Err)lu.w1 != NT_LU_OK)
            return -1;
        console_tty = (Handle)lu.w2;
    }
    return console_tty;
}

static void    *fsd_buf  = NULL;
static uint32_t fsd_size = 0;

/*
 * fsd_buf is a single-transaction resource shared by every thread: each
 * request is staged into it at fixed offsets and the reply is read back
 * from it after the (blocking) msg_call. Two threads staging at once would
 * corrupt each other, so a transaction must own the buffer from staging
 * through reply.
 *
 * That ownership window spans a blocking msg_call, so it cannot be a lock:
 * fsd_gate (a zzuspin spin lock) is held only long enough to flip fsd_busy,
 * never across the call. A thread that finds the buffer busy drops the gate
 * and yields, so fsd and everything else keep running while the owner is
 * blocked. One fsd transaction is in flight at a time.
 */
static zzuspin_t fsd_gate = ZZUSPIN_INIT;
static int       fsd_busy = 0;

static void fsd_claim(void)
{
    for (;;) {
        zzuspin_lock(&fsd_gate);
        if (!fsd_busy) { fsd_busy = 1; zzuspin_unlock(&fsd_gate); return; }
        zzuspin_unlock(&fsd_gate);
        ZuzuYield();
    }
}

static void fsd_release(void)
{
    zzuspin_lock(&fsd_gate);
    fsd_busy = 0;
    zzuspin_unlock(&fsd_gate);
}

static int fsd_connect(void) {
    if (fsd_buf) return 0;
    Message lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("fsd\0"), 0);
    if ((Err)lu.w1 != NT_LU_OK) return -1;
    fsd_handle = (Handle)lu.w2;
    uint32_t fsd_pid = lu.w3;

    Handle shm = zuzu_shm_create(FSD_SHM_DEFAULT);
    if (shm < 0) return -1;

    void *p = zuzu_memmap(shm, 0, PROT_RW, 0);
    if (zuzu_is_err(p)) return -1;

    int32_t slot = zuzu_grant(shm, (int32_t)fsd_pid);
    if (slot < 0) return -1;

    Message r = zuzu_msg_call(fsd_handle, FSD_SET_BUF,
                            FSD_SETBUF_PACK(slot, FSD_SHM_DEFAULT), 0);
    if ((Err)r.w1 != ZUZU_OK) return -1;

    fsd_buf  = p;
    fsd_size = FSD_SHM_DEFAULT;
    return 0;
}

void *_sbrk(intptr_t incr) {
    void *p = sbrk(incr);
    if (p == (void *)-1)
        errno = ENOMEM;
    return p;
}

int _isatty(int file) {
    return (file >= 0 && file <= 2) ? 1 : 0;
}

/* There is no real termios layer here (no tcgetattr/tcsetattr), just this one
 * bit: raw-mode readers that do their own line editing (e.g. a full-screen
 * editor reading one keystroke at a time) call this to stop _read() from
 * rewriting their input, see the ICRNL comment in _read() below. */
int zuzu_console_set_raw(int enable) {
    console_raw_mode = enable ? 1 : 0;
    return 0;
}


int _write(int file, char *ptr, int len)
{

    if (len == 0) return 0;
    if (len < 0)  { errno = EINVAL; return -1; }

    /* stdout/stderr -> console */
    if (file == 1 || file == 2) {
        Handle tty = console_port();
        if (tty < 0) { errno = EIO; return -1; }

        size_t off = 0;
        while (off < (size_t)len) {
            uint32_t chunk = (uint32_t)((size_t)len - off);
            if (chunk > LMSG_BUF_SIZE) chunk = LMSG_BUF_SIZE;
            lmsg_write(ptr + off, chunk);
            zuzu_msg_lsend(tty, chunk);
            off += chunk;
        }
        return len;
    }

    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    uint32_t cap = fsd_size - FSD_DATA_OFF;
    if (cap > 0xFFFFu) cap = 0xFFFFu;        /* count is 16 bits in the packing */
    size_t off = 0;

    fsd_claim();
    while (off < (size_t)len) {
        uint32_t chunk = (uint32_t)((size_t)len - off);
        if (chunk > cap) chunk = cap;

        memcpy((uint8_t *)fsd_buf + FSD_DATA_OFF, ptr + off, chunk);

        Message r = zuzu_msg_call(fsd_handle, FSD_WRITE,
                                ((uint32_t)fsd_fd[file] & 0xFFFFu) | (chunk << 16), 0);
        if ((Err)r.w1 != ZUZU_OK) {
            if (off) break;                  /* partial write wins */
            fsd_release();
            errno = err_to_errno((Err)r.w1);
            return -1;
        }

        uint32_t put = r.w2;
        if (put > chunk) put = chunk;
        off += put;

        if (put < chunk) break;              /* disk full or short write */
    }
    fsd_release();

    return (int)off;
}

void __attribute__((noreturn)) _exit(int status) {
    ZuzuPQuit(status);
    for(;;);
}

int _read(int file, char *ptr, int len)
{

    if (len <= 0) return len == 0 ? 0 : (errno = EINVAL, -1);


    if (file == 0) {
        Handle tty = console_port();
        if (tty < 0) { errno = EIO; return -1; }

        uint32_t want = (uint32_t)len;
        if (want > LMSG_BUF_SIZE) want = LMSG_BUF_SIZE;


        /* The console driver's read is non-blocking: it replies with whatever
         * is in its RX ring, which is usually nothing. read(2) on a terminal
         * must block until at least one byte arrives -- returning 0 here would
         * mean EOF, and newlib latches EOF on the stream, so the first scanf()
         * would fail and every later one would too. Poll until a byte lands;
         * a real terminal read blocks indefinitely, so this must too -- a
         * caller idle for more than a few seconds between keystrokes is not
         * an error (raw-mode readers like a full-screen editor treat any
         * negative return as fatal and exit()). */
        for (;;) {
            Message r = zuzu_msg_lcall(tty, want);
            if ((int32_t)r.w0 < 0) { errno = EIO; return -1; }

            uint32_t got = r.w1;
            if (got > want) got = want;
            if (got) {
                memcpy(ptr, lmsg_buf(), got);
                /* ICRNL: Enter on a serial console sends CR. scanf() would
                 * cope -- CR is whitespace -- but fgets/getline look for LF
                 * specifically and would never see a line end. The TX side
                 * already expands LF to CRLF in pl011drv, so translating on
                 * the way in just completes the pair.
                 *
                 * Raw-mode readers (single-keystroke editors) want the literal
                 * byte a terminal's Enter key sends and do their own line
                 * handling, so this translation must not run for them --
                 * doing so silently turns their Enter into an ordinary
                 * character instead of a line break. */
                if (!console_raw_mode)
                    for (uint32_t i = 0; i < got; i++)
                        if (ptr[i] == '\r') ptr[i] = '\n';
                return (int)got;
            }
            ZuzuSleep(5);
        }
    }

    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    uint32_t cap = fsd_size - FSD_DATA_OFF;
    size_t   off = 0;

    fsd_claim();
    while (off < (size_t)len) {
        uint32_t chunk = (uint32_t)((size_t)len - off);
        if (chunk > cap) chunk = cap;

        Message r = zuzu_msg_call(fsd_handle, FSD_READ,
                                ((uint32_t)fsd_fd[file] & 0xFFFFu) | (chunk << 16), 0);
        if ((Err)r.w1 != ZUZU_OK) {
            if (off) break;                  /* partial success wins */
            fsd_release();
            errno = err_to_errno((Err)r.w1);
            return -1;
        }

        uint32_t got = r.w2;
        if (got > chunk) got = chunk;        /* never trust the server's count */
        if (got) memcpy(ptr + off, (const uint8_t *)fsd_buf + FSD_DATA_OFF, got);
        off += got;

        if (got < chunk) break;              /* short read = EOF */
    }
    fsd_release();

    return (int)off;
}

int _close(int file) {
    if (file >= 0 && file <= 2) return 0;
    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    Message r = zuzu_msg_call(fsd_handle, FSD_CLOSE, (uint32_t)fsd_fd[file], 0);
    fsd_fd[file] = -1;                     /* free the slot regardless */

    if ((Err)r.w1 != ZUZU_OK) { errno = err_to_errno((Err)r.w1); return -1; }
    return 0;
}

int _lseek(int file, int ptr, int dir)
{

    if (file >= 0 && file <= 2) { errno = ESPIPE; return -1; }   /* terminals don't seek */
    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }
    fsd_req_t req;
    memset(&req, 0, sizeof(req));
    req.size     = sizeof(req);
    req.cmd      = FSD_SEEK;
    req.data_off = FSD_DATA_OFF;   /* no payload, but must pass validation */
    req.data_len = 0;
    req.fd       = (uint32_t)fsd_fd[file];
    req.offset   = (int64_t)ptr;
    req.whence   = (uint32_t)dir;  /* SEEK_SET/CUR/END == FSD_SEEK_* */

    fsd_claim();
    memcpy((uint8_t *)fsd_buf + FSD_REQ_OFF, &req, sizeof(req));

    Message r = zuzu_msg_call(fsd_handle, FSD_SEEK, 0, 0);
    if ((Err)r.w1 != ZUZU_OK) { fsd_release(); errno = err_to_errno((Err)r.w1); return -1; }

    fsd_release();
    return (int)r.w2;   /* new absolute offset (truncated to 32 bits) */
}

int _getpid(void) {
    return ZuzuGetPid();
}

int _kill(int pid, int sig) {
    if (pid == ZuzuGetPid())
        ZuzuPQuit(sig);
    errno = EINVAL; return -1;
}

int _fstat(int file, struct stat *st)
{

    memset(st, 0, sizeof(*st));

    /* console fds: character device, which is what makes newlib line-buffer */
    if (file >= 0 && file <= 2) {
        st->st_mode = S_IFCHR;
        return 0;
    }

    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    fsd_claim();
    Message r = zuzu_msg_call(fsd_handle, FSD_FSTAT,
                            (uint32_t)fsd_fd[file], 0);
    if ((Err)r.w1 != ZUZU_OK) { fsd_release(); errno = err_to_errno((Err)r.w1); return -1; }

    fsd_stat_t fst;
    memcpy(&fst, (const uint8_t *)fsd_buf + FSD_DATA_OFF, sizeof(fst));
    fsd_release();

    st->st_mode  = (fst.type == FSD_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size  = (off_t)fst.size;
    st->st_blksize = 512;
    return 0;
}

int _open(const char *name, int flags, ...) {
    /* Held across connect + slot allocation too: fsd_connect() and the
     * fsd_fd[] scan both touch shared state, and the request stages into
     * the shared buffer. */
    fsd_claim();
    if (fsd_connect() < 0) { fsd_release(); errno = EIO; return -1; }

    /* find a free POSIX fd, 3 and up */
    int pfd = 3;
    while (pfd < MAX_FD && fsd_fd[pfd] >= 0) pfd++;
    if (pfd == MAX_FD) { fsd_release(); errno = EMFILE; return -1; }

    /* path into the payload region */
    size_t plen = strlen(name);
    if (plen + 1 > fsd_size - FSD_DATA_OFF) { fsd_release(); errno = ENAMETOOLONG; return -1; }
    memcpy((uint8_t *)fsd_buf + FSD_DATA_OFF, name, plen + 1);

    /* request struct */
    fsd_req_t req;
    memset(&req, 0, sizeof(req));
    req.size     = sizeof(req);
    req.cmd      = FSD_OPEN;
    req.data_off = FSD_DATA_OFF;
    req.data_len = plen + 1;
    req.mode     = flags_to_fsd_mode(flags);
    memcpy((uint8_t *)fsd_buf + FSD_REQ_OFF, &req, sizeof(req));

    Message r = zuzu_msg_call(fsd_handle, FSD_OPEN, 0, 0);
    if ((Err)r.w1 != ZUZU_OK) { fsd_release(); errno = err_to_errno((Err)r.w1); return -1; }

    fsd_fd[pfd] = (int)r.w2;
    fsd_release();
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

    const syspage_t *sp = (const syspage_t *)SYSPAGE_VA;
    uint32_t hz    = sp->tick_hz ? sp->tick_hz : 1000u;
    uint64_t ticks = sp->uptime_ticks;

    tv->tv_sec  = (time_t)(sp->boot_time_s + ticks / hz);
    tv->tv_usec = (suseconds_t)((ticks % hz) * 1000000ull / hz);
    return 0;
}

/* Elapsed real time in scheduler ticks; no user/kernel split is tracked. */
clock_t _times(struct tms *buf)
{
    const syspage_t *sp = (const syspage_t *)SYSPAGE_VA;
    clock_t ticks = (clock_t)sp->uptime_ticks;

    if (buf) {
        buf->tms_utime  = ticks;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return ticks;
}

int _stat(const char *name, struct stat *st)
{
    if (!name || !st) { errno = EFAULT; return -1; }

    fsd_claim();
    if (fsd_connect() < 0) { fsd_release(); errno = EIO; return -1; }

    size_t plen = strlen(name);
    if (plen + 1 > fsd_size - FSD_DATA_OFF) { fsd_release(); errno = ENAMETOOLONG; return -1; }
    memcpy((uint8_t *)fsd_buf + FSD_DATA_OFF, name, plen + 1);

    fsd_req_t req;
    memset(&req, 0, sizeof(req));
    req.size     = sizeof(req);
    req.cmd      = FSD_STAT;
    req.data_off = FSD_DATA_OFF;
    req.data_len = plen + 1;
    memcpy((uint8_t *)fsd_buf + FSD_REQ_OFF, &req, sizeof(req));

    Message r = zuzu_msg_call(fsd_handle, FSD_STAT, 0, 0);
    if ((Err)r.w1 != ZUZU_OK) { fsd_release(); errno = err_to_errno((Err)r.w1); return -1; }

    fsd_stat_t fst;
    memcpy(&fst, (const uint8_t *)fsd_buf + FSD_DATA_OFF, sizeof(fst));
    fsd_release();

    memset(st, 0, sizeof(*st));
    st->st_mode    = (fst.type == FSD_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size    = (off_t)fst.size;
    st->st_blksize = 512;
    return 0;
}

int _unlink(const char *name)
{
    if (!name) { errno = EFAULT; return -1; }

    fsd_claim();
    if (fsd_connect() < 0) { fsd_release(); errno = EIO; return -1; }

    size_t plen = strlen(name);
    if (plen + 1 > fsd_size - FSD_DATA_OFF) { fsd_release(); errno = ENAMETOOLONG; return -1; }
    memcpy((uint8_t *)fsd_buf + FSD_DATA_OFF, name, plen + 1);

    fsd_req_t req;
    memset(&req, 0, sizeof(req));
    req.size     = sizeof(req);
    req.cmd      = FSD_UNLINK;
    req.data_off = FSD_DATA_OFF;
    req.data_len = plen + 1;
    memcpy((uint8_t *)fsd_buf + FSD_REQ_OFF, &req, sizeof(req));

    Message r = zuzu_msg_call(fsd_handle, FSD_UNLINK, 0, 0);
    if ((Err)r.w1 != ZUZU_OK) { fsd_release(); errno = err_to_errno((Err)r.w1); return -1; }
    fsd_release();
    return 0;
}

/* fsd exposes rename but no hard-link command, so there is nothing to call. */
int _link(const char *existing, const char *newpath)
{
    (void)existing; (void)newpath;
    errno = ENOSYS;
    return -1;
}