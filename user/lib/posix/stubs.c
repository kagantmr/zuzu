#include <unistd.h>      
#include <sys/stat.h>    
#include <errno.h>
#include <fcntl.h>
#include <zuzu/zuzu.h>
#include <zuzu/lmsg.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/fsd_protocol.h>
#include <string.h>

#define MAX_FD 32

static handle_t console_tty = -1;
extern void *sbrk(intptr_t incr);
static int fsd_fd[MAX_FD] = { [0 ... MAX_FD - 1] = -1 };
static handle_t fsd_handle = -1;

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

/* zuzu err_t -> POSIX errno. */
static int err_to_errno(err_t e)
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

static handle_t console_port(void) {
    if (console_tty < 0) {
        msg_t lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("tty0"), 0);
        if ((err_t)lu.r1 != NT_LU_OK)
            return -1;
        console_tty = (handle_t)lu.r2;
    }
    return console_tty;
}

static void    *fsd_buf  = NULL;
static uint32_t fsd_size = 0;

static int fsd_connect(void) {
    if (fsd_buf) return 0;
    msg_t lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("fsd\0"), 0);
    if ((err_t)lu.r1 != NT_LU_OK) return -1;
    fsd_handle = (handle_t)lu.r2;
    uint32_t fsd_pid = lu.r3;

    handle_t shm = zuzu_shm_create(FSD_SHM_DEFAULT);
    if (shm < 0) return -1;

    void *p = zuzu_memmap(shm, 0, VM_PROT_RW, 0);
    if (zuzu_is_err(p)) return -1;

    int32_t slot = zuzu_grant(shm, (int32_t)fsd_pid);
    if (slot < 0) return -1;

    msg_t r = zuzu_msg_call(fsd_handle, FSD_SET_BUF,
                            FSD_SETBUF_PACK(slot, FSD_SHM_DEFAULT), 0);
    if ((err_t)r.r1 != ZUZU_OK) return -1;

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


int _write(int file, char *ptr, int len)
{

    if (len == 0) return 0;
    if (len < 0)  { errno = EINVAL; return -1; }

    /* stdout/stderr -> console */
    if (file == 1 || file == 2) {
        handle_t tty = console_port();
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

    while (off < (size_t)len) {
        uint32_t chunk = (uint32_t)((size_t)len - off);
        if (chunk > cap) chunk = cap;

        memcpy((uint8_t *)fsd_buf + FSD_DATA_OFF, ptr + off, chunk);

        msg_t r = zuzu_msg_call(fsd_handle, FSD_WRITE,
                                ((uint32_t)fsd_fd[file] & 0xFFFFu) | (chunk << 16), 0);
        if ((err_t)r.r1 != ZUZU_OK) {
            if (off) break;                  /* partial write wins */
            errno = err_to_errno((err_t)r.r1);
            return -1;
        }

        uint32_t put = r.r2;
        if (put > chunk) put = chunk;
        off += put;

        if (put < chunk) break;              /* disk full or short write */
    }

    return (int)off;
}

void __attribute__((noreturn)) _exit(int status) {
    zuzu_pquit(status);
    for(;;);
}

int _read(int file, char *ptr, int len)
{

    if (len <= 0) return len == 0 ? 0 : (errno = EINVAL, -1);


    if (file == 0) {
        handle_t tty = console_port();
        if (tty < 0) { errno = EIO; return -1; }

        uint32_t want = (uint32_t)len;
        if (want > LMSG_BUF_SIZE) want = LMSG_BUF_SIZE;


        /* The console driver's read is non-blocking: it replies with whatever
         * is in its RX ring, which is usually nothing. read(2) on a terminal
         * must block until at least one byte arrives -- returning 0 here would
         * mean EOF, and newlib latches EOF on the stream, so the first scanf()
         * would fail and every later one would too. Poll until a byte lands. */
        for (int spin = 0; spin < 400; spin++) {   /* DEBUG: bounded */
            msg_t r = zuzu_msg_lcall(tty, want);
            if ((int32_t)r.r0 < 0) { errno = EIO; return -1; }

            uint32_t got = r.r1;
            if (got > want) got = want;
            if (got) {
                memcpy(ptr, lmsg_buf(), got);
                /* ICRNL: Enter on a serial console sends CR. scanf() would
                 * cope -- CR is whitespace -- but fgets/getline look for LF
                 * specifically and would never see a line end. The TX side
                 * already expands LF to CRLF in pl011drv, so translating on
                 * the way in just completes the pair. */
                for (uint32_t i = 0; i < got; i++)
                    if (ptr[i] == '\r') ptr[i] = '\n';
                return (int)got;
            }
            zuzu_sleep(5);
        }
    }

    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    uint32_t cap = fsd_size - FSD_DATA_OFF;
    size_t   off = 0;

    while (off < (size_t)len) {
        uint32_t chunk = (uint32_t)((size_t)len - off);
        if (chunk > cap) chunk = cap;

        msg_t r = zuzu_msg_call(fsd_handle, FSD_READ,
                                ((uint32_t)fsd_fd[file] & 0xFFFFu) | (chunk << 16), 0);
        if ((err_t)r.r1 != ZUZU_OK) {
            if (off) break;                  /* partial success wins */
            errno = err_to_errno((err_t)r.r1);
            return -1;
        }

        uint32_t got = r.r2;
        if (got > chunk) got = chunk;        /* never trust the server's count */
        if (got) memcpy(ptr + off, (const uint8_t *)fsd_buf + FSD_DATA_OFF, got);
        off += got;

        if (got < chunk) break;              /* short read = EOF */
    }

    return (int)off;
}

int _close(int file) {
    if (file >= 0 && file <= 2) return 0;
    if (!fsd_buf || file < 3 || file >= MAX_FD || fsd_fd[file] < 0) { errno = EBADF; return -1; }

    msg_t r = zuzu_msg_call(fsd_handle, FSD_CLOSE, (uint32_t)fsd_fd[file], 0);
    fsd_fd[file] = -1;                     /* free the slot regardless */

    if ((err_t)r.r1 != ZUZU_OK) { errno = err_to_errno((err_t)r.r1); return -1; }
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

    memcpy((uint8_t *)fsd_buf + FSD_REQ_OFF, &req, sizeof(req));

    msg_t r = zuzu_msg_call(fsd_handle, FSD_SEEK, 0, 0);
    if ((err_t)r.r1 != ZUZU_OK) { errno = err_to_errno((err_t)r.r1); return -1; }

    return (int)r.r2;   /* new absolute offset (truncated to 32 bits) */
}

int _getpid(void) {
    return zuzu_getpid();
}

int _kill(int pid, int sig) {
    if (pid == zuzu_getpid())
        zuzu_pquit(sig);
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

    msg_t r = zuzu_msg_call(fsd_handle, FSD_FSTAT,
                            (uint32_t)fsd_fd[file], 0);
    if ((err_t)r.r1 != ZUZU_OK) { errno = err_to_errno((err_t)r.r1); return -1; }

    fsd_stat_t fst;
    memcpy(&fst, (const uint8_t *)fsd_buf + FSD_DATA_OFF, sizeof(fst));

    st->st_mode  = (fst.type == FSD_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size  = (off_t)fst.size;
    st->st_blksize = 512;
    return 0;
}

int _open(const char *name, int flags, ...) {
    if (fsd_connect() < 0) { errno = EIO; return -1; }

    /* find a free POSIX fd, 3 and up */
    int pfd = 3;
    while (pfd < MAX_FD && fsd_fd[pfd] >= 0) pfd++;
    if (pfd == MAX_FD) { errno = EMFILE; return -1; }

    /* path into the payload region */
    size_t plen = strlen(name);
    if (plen + 1 > fsd_size - FSD_DATA_OFF) { errno = ENAMETOOLONG; return -1; }
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

    msg_t r = zuzu_msg_call(fsd_handle, FSD_OPEN, 0, 0);
    if ((err_t)r.r1 != ZUZU_OK) { errno = err_to_errno((err_t)r.r1); return -1; }

    fsd_fd[pfd] = (int)r.r2;
    return pfd;
}