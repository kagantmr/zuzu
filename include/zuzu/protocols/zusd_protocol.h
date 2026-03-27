#ifndef ZUSD_PROTOCOL_H
#define ZUSD_PROTOCOL_H

/*
 * ZUSD_CMD_GET_BUF (call)
 *   req:  r2=ZUSD_CMD_GET_BUF
 *   resp: r1=0 ok, r2=shmem_handle (512-byte shared buffer)
 *
 * ZUSD_CMD_READ (call)
 *   req:  r2=ZUSD_CMD_READ, r3=block_num
 *   resp: r1=0 ok, -1 err  - result written into shared buffer
 *
 * ZUSD_CMD_WRITE (call)
 *   req:  r2=ZUSD_CMD_WRITE, r3=block_num
 *   resp: r1=0 ok, -1 err  - data read from shared buffer
 */
#define ZUSD_CMD_GET_BUF 1
#define ZUSD_CMD_READ    2
#define ZUSD_CMD_WRITE   3

#endif