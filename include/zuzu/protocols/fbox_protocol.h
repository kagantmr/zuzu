#ifndef FBOX_PROTOCOL_H
#define FBOX_PROTOCOL_H

#define FBOX_OPEN 1
#define FBOX_READ 2
#define FBOX_WRITE 3
#define FBOX_CLOSE 4
#define FBOX_READDIR 5
#define FBOX_STAT 6

#define FBOX_GET_BUF 7

#define FBOX_ERR_NOENT (-1)
#define FBOX_ERR_ISDIR (-2)
#define FBOX_ERR_NOTDIR (-3)
#define FBOX_ERR_BADFD (-4)
#define FBOX_ERR_MAXFD (-5)
#define FBOX_ERR_IO (-6)

#endif