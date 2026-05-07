#include <zuzu/fmt.h>
#include <string.h>
#include <mem.h>

void fmt_writer_init(fmt_writer_t *w, void *buf, uint32_t size) {
    w->buf = (uint8_t *)buf;
    w->size = size;
    w->pos = 0;
    w->overflow = 0;
}

static void _ensure(fmt_writer_t *w, uint32_t need) {
    if (w->overflow) return;
    if (w->pos + need > w->size) w->overflow = 1;
}

void fmt_put_u8(fmt_writer_t *w, uint8_t v) {
    _ensure(w, 1);
    if (!w->overflow) w->buf[w->pos++] = v;
}

void fmt_put_u32(fmt_writer_t *w, uint32_t v) {
    _ensure(w, 4);
    if (w->overflow) return;
    memcpy(&w->buf[w->pos], &v, 4);
    w->pos += 4;
}

void fmt_put_str(fmt_writer_t *w, const char *s) {
    uint32_t len = (uint32_t)(strlen(s) + 1);
    fmt_put_u32(w, len);
    _ensure(w, len);
    if (!w->overflow) {
        memcpy(&w->buf[w->pos], s, len);
        w->pos += len;
    }
}

void fmt_put_bytes(fmt_writer_t *w, const void *p, uint32_t len) {
    fmt_put_u32(w, len);
    _ensure(w, len);
    if (!w->overflow) {
        memcpy(&w->buf[w->pos], p, len);
        w->pos += len;
    }
}

uint32_t fmt_finish(fmt_writer_t *w) {
    return w->overflow ? 0 : w->pos;
}

void fmt_reader_init(fmt_reader_t *r, const void *buf, uint32_t size) {
    r->buf = (const uint8_t *)buf;
    r->size = size;
    r->pos = 0;
}

uint8_t fmt_get_u8(fmt_reader_t *r) {
    if (r->pos + 1 > r->size) return 0;
    return r->buf[r->pos++];
}

uint32_t fmt_get_u32(fmt_reader_t *r) {
    if (r->pos + 4 > r->size) return 0;
    uint32_t v;
    memcpy(&v, &r->buf[r->pos], 4);
    r->pos += 4;
    return v;
}

char *fmt_get_str(fmt_reader_t *r, char *out, uint32_t max) {
    uint32_t len = fmt_get_u32(r);
    if (len == 0 || r->pos + len > r->size) return NULL;
    uint32_t tocopy = len < max ? len : (max - 1);
    memcpy(out, &r->buf[r->pos], tocopy);
    out[tocopy] = '\0';
    r->pos += len;
    return out;
}

uint32_t fmt_get_bytes(fmt_reader_t *r, void *out, uint32_t max) {
    uint32_t len = fmt_get_u32(r);
    if (r->pos + len > r->size) return 0;
    uint32_t tocopy = len < max ? len : max;
    memcpy(out, &r->buf[r->pos], tocopy);
    r->pos += len;
    return tocopy;
}

int fmt_ok(const fmt_reader_t *r) {
    return (int)(r->pos <= r->size);
}
