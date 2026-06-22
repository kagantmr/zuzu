#include <zuzu/err.h>

const char *strerror(err_t err) {
    switch (err) {
    case ZUZU_OK:
        return "ERR_OK";
    case ERR_BADARG:
        return "ERR_BADARG";
    case ERR_MALFORMED:
        return "ERR_MALFORMED";
    case ERR_DEAD:
        return "ERR_DEAD";
    case ERR_BUSY:
        return "ERR_BUSY";
    case ERR_NOMEM:
        return "ERR_NOMEM";
    case ERR_NOMATCH:
        return "ERR_NOMATCH";
    case ERR_NOENT:
        return "ERR_NOENT";
    case ERR_NOPERM:
        return "ERR_NOPERM";
    case ERR_BADPTR:
        return "ERR_BADPTR";
    case ERR_TIMEOUT:
        return "ERR_TIMEOUT";
    case ERR_OVERFLOW:
        return "ERR_OVERFLOW";
    case ERR_BUFFULL:
        return "ERR_BUFFULL";
    case ERR_SYSDOWN:
        return "ERR_SYSDOWN";
    case ERR_DUPLICATE:
        return "ERR_DUPLICATE";
    default:
        return "ERR_UNKNOWN";
    }
}