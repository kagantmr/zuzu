#include <zuzu/err.h>

const char *strtoerror(err_t err) {
    switch (err) {
    case ZUZU_OK:
        return "ERR_OK";
    /* frozen kernel band */
    case ERR_NOPERM:
        return "ERR_NOPERM";
    case ERR_NOENT:
        return "ERR_NOENT";
    case ERR_BUSY:
        return "ERR_BUSY";
    case ERR_NOMEM:
        return "ERR_NOMEM";
    case ERR_BADTYPE:
        return "ERR_BADTYPE";
    case ERR_BADARG:
        return "ERR_BADARG";
    case ERR_NOSYS:
        return "ERR_NOSYS";
    case ERR_BADPTR:
        return "ERR_BADPTR";
    case ERR_DEAD:
        return "ERR_DEAD";
    case ERR_TIMEOUT:
        return "ERR_TIMEOUT";
    case ERR_OVERFLOW:
        return "ERR_OVERFLOW";
    case ERR_BADHANDLE:
        return "ERR_BADHANDLE";
    /* zuzuOS server/library band */
    case ERR_BUFFULL:
        return "ERR_BUFFULL";
    case ERR_BUFEMPTY:
        return "ERR_BUFEMPTY";
    case ERR_SYSDOWN:
        return "ERR_SYSDOWN";
    case ERR_NOTCONN:
        return "ERR_NOTCONN";
    case ERR_DUPLICATE:
        return "ERR_DUPLICATE";
    case ERR_MALFORMED:
        return "ERR_MALFORMED";
    case ERR_IO:
        return "ERR_IO";
    default:
        break;
    }

    /* Unknown code: format the number so post-freeze callers that meet a code
     * they weren't compiled against still print something useful. Not reentrant;
     * fine for the single-threaded diagnostic paths that use it. */
    static char buf[16];
    int32_t v = (int32_t)err;
    int neg = v < 0;
    uint32_t n = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    } while (n);

    int p = 0;
    buf[p++] = 'E';
    buf[p++] = 'R';
    buf[p++] = 'R';
    buf[p++] = ' ';
    if (neg)
        buf[p++] = '-';
    while (i > 0)
        buf[p++] = tmp[--i];
    buf[p] = '\0';
    return buf;
}