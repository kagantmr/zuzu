#ifndef FAT32D_PROTOCOL_H
#define FAT32D_PROTOCOL_H

#define FAT32_OPEN 1
#define FAT32_READ 2
#define FAT32_WRITE 3
#define FAT32_CLOSE 4
#define FAT32_READDIR 5
#define FAT32_STAT 6

#define FAT32_GET_BUF 7

#define FAT32_ERR_NOENT (-1)
#define FAT32_ERR_ISDIR (-2)
#define FAT32_ERR_NOTDIR (-3)
#define FAT32_ERR_BADFD (-4)
#define FAT32_ERR_MAXFD (-5)
#define FAT32_ERR_IO (-6)

#endif