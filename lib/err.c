#include <zuzu/err.h>

const char *zuzu_strerror(zuzu_err_t err) {
    switch (err) {
    case ZUZU_OK:
        return "ERR_OK";
    case ZUZU_ERR_BADARG:
        return "ERR_BADARG";
    case ZUZU_ERR_BADFORM:
        return "ERR_BADFORM";
    case ZUZU_ERR_DEAD:
        return "ERR_DEAD";
    case ZUZU_ERR_BUSY:
        return "ERR_BUSY";
    case ZUZU_ERR_NOMEM:
        return "ERR_NOMEM";
    case ZUZU_ERR_NOMATCH:
        return "ERR_NOMATCH";
    case ZUZU_ERR_NOENT:
        return "ERR_NOENT";
    case ZUZU_ERR_NOPERM:
        return "ERR_NOPERM";
    case ZUZU_ERR_PTRFAULT:
        return "ERR_PTRFAULT";
    case ZUZU_ERR_TIMEOUT:
        return "ERR_TIMEOUT";
    default:
        return "ERR_UNKNOWN";
    }
}