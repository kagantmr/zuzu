#ifndef SOCKETD_PROTOCOL_H
#define SOCKETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOCKETD_CREATE 1
#define SOCKETD_BIND 2
#define SOCKETD_LISTEN 3
#define SOCKETD_ACCEPT 4
#define SOCKETD_CONNECT 5
#define SOCKETD_SEND 6
#define SOCKETD_RECV 7
#define SOCKETD_CLOSE 8

// zuzu error types...

#ifdef __cplusplus
}
#endif

#endif // SOCKETD_PROTOCOL_H