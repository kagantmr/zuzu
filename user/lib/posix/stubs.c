#include <unistd.h>      
#include <sys/stat.h>    
#include <errno.h>
#include <zuzu/zuzu.h>
#include <zuzu/lmsg.h>
#include <zuzu/protocols/nt_protocol.h>
#include <string.h>

static int32_t console_tty = -1;
static char stdio_tty_name[4] = {'t', 't', 'y', '0'};
extern void *sbrk(intptr_t incr);

static int32_t console_port(void) {
    if (console_tty < 0) {
        msg_t lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack(stdio_tty_name), 0);
        if ((int32_t)lu.r1 != ZUZU_OK)
            return -1;
        console_tty = (int32_t)lu.r2;
    }
    return console_tty;
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


int _write(int file, char *ptr, int len) {
    if (file != 1 && file != 2) { errno = EBADF; return -1; }
    int32_t tty = console_port();
    if (tty < 0) { errno = EIO; return -1; }
    if (len == 0) return 0;
    if (len < 0)  { errno = EINVAL; return -1; }

    /* chunk loop — same as vprintf's */
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

void __attribute__((noreturn)) _exit(int status) {
    zuzu_pquit(status);
    for(;;);
}

int _read(int file, char *ptr, int len) {
    (void)ptr; (void)len;
    errno = ENOSYS; return -1;
}

int _close(int file) {
    if (_isatty(file))
        return 0;
    errno = EBADF; return -1;
}

int _lseek(int file, int ptr, int dir) {
    (void)ptr; (void)dir;
    errno = ESPIPE; return -1;
}

int _getpid(void) {
    return zuzu_getpid();
}

int _kill(int pid, int sig) {
    if (pid == zuzu_getpid())
        zuzu_pquit(sig);
    errno = EINVAL; return -1;
}

int _fstat(int file, struct stat *st) {
    memset(st,0, sizeof(struct stat));
    st->st_mode = S_IFCHR;
    return 0;
}
