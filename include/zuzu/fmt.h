#ifndef ZUZU_FMT_H
#define ZUZU_FMT_H

#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint32_t size;
    uint32_t pos;
    int      overflow;
} fmt_writer_t;

typedef struct {    
    const uint8_t *buf;
    uint32_t       size;
    uint32_t       pos;
} fmt_reader_t;

void fmt_writer_init(fmt_writer_t *w, void *buf, uint32_t size);
void fmt_put_u8(fmt_writer_t *w, uint8_t v);
void fmt_put_u32(fmt_writer_t *w, uint32_t v);
void fmt_put_str(fmt_writer_t *w, const char *s);       // length-prefixed
void fmt_put_bytes(fmt_writer_t *w, const void *p, uint32_t len);
uint32_t fmt_finish(fmt_writer_t *w);  // returns bytes written, or 0 on overflow

void     fmt_reader_init(fmt_reader_t *r, const void *buf, uint32_t size);
uint8_t  fmt_get_u8(fmt_reader_t *r);
uint32_t fmt_get_u32(fmt_reader_t *r);
char    *fmt_get_str(fmt_reader_t *r, char *out, uint32_t max);
uint32_t fmt_get_bytes(fmt_reader_t *r, void *out, uint32_t max);
int      fmt_ok(const fmt_reader_t *r);  // 0 if overread

#endif // ZUZU_FMT_H