/**
 * @file err.c
 * @brief Implementation of the StrToError function.
 */

#include "err.h"

const char *StrToError(Err err)
{
    switch (err) {
    case ZUZU_OK:
        return "Success";
    case ERR_NOPERM:
        return "Permission denied";
    case ERR_NOENT:
        return "Entry not found";
    case ERR_BUSY:
        return "Object is busy";
    case ERR_NOMEM:
        return "Out of memory";
    case ERR_BADTYPE:
        return "Invalid type";
    case ERR_BADARG:
        return "Invalid arguments";
    case ERR_NOSYS:
        return "Function not implemented";
    case ERR_BADPTR:
        return "Bad address";
    case ERR_DEAD:
        return "Object or peer no longer exists";
    case ERR_TIMEOUT:
        return "Wait timed out";
    case ERR_OVERFLOW:
        return "Size exceeds the limit";
    case ERR_BADHANDLE:
        return "Invalid handle";
    case ERR_BUFFULL:
        return "Destination full";
    case ERR_BUFEMPTY:
        return "Source empty";
    case ERR_SYSDOWN:
        return "System is down";
    case ERR_NOTCONN:
        return "Not connected";
    case ERR_DUPLICATE:
        return "Duplicate request";
    case ERR_MALFORMED:
        return "Malformed data";
    case ERR_IO:
        return "Input/output error";
    case ERR_OUTDATED:
        return "Cannot work in outdated system";
    case ERR_CONNREFUSED:
        return "Connection refused";
    case ERR_CONNRESET:
        return "Connection reset";
    default:
        return "Unknown error";
    }
}
