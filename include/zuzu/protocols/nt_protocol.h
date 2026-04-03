#ifndef ZUZU_NT_PROTOCOL_H
#define ZUZU_NT_PROTOCOL_H

#define NT_PORT 0
#define NT_NAME_SYS "sys"

#define NT_REGISTER 1
#define NT_LOOKUP 2
#define DEN_CREATE 3
#define DEN_INVITE 4
#define DEN_KICK 5
#define DEN_MYDEN  6
#define DEN_MYDEN_COUNT 7

#define NT_LU_OK 0
#define NT_LU_NOMATCH (-1)
#define NT_REG_FAIL (-2)
#define NT_REG_OK 0
#define NT_BADCMD (-3)
#define DEN_OK         0
#define DEN_FAIL      (-4)
#define DEN_FULL      (-5)

#endif